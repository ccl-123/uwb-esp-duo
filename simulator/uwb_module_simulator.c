#include "uwb_module_simulator.h"
#include "esp_log.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h> 

static const char* TAG_SIM = "UWB_SIMULATOR"; 

/* ------------------------- 模拟器内部常量定义 ------------------------- */
#define SIM_RX_LINE_BUF_SIZE 256         ///< 模拟器AT指令行接收缓冲区大小
#define SIM_RANGING_FRAME_SIZE 8         ///< 模拟的测距数据帧固定大小
#define SIM_DEFAULT_POWER_LEVEL 3        ///< 模拟的默认功率等级 (14dBm)



#define SIM_UART_EVENT_TASK_PRIORITY 10  ///< 模拟器UART事件任务优先级

#define SIM_DEFAULT_DIST_MIN 50          ///< 模拟测距最小值 (cm)
#define SIM_DEFAULT_DIST_RANGE 200       ///< 模拟测距范围 (cm)
#define SIM_DEFAULT_RSSI_BASE (-50)      ///< 模拟RSSI基准值 (dBm)
#define SIM_DEFAULT_RSSI_RANGE 40        ///< 模拟RSSI变化范围


// 模拟器内部状态变量
static uart_port_t g_sim_uart_port; ///< 模拟器使用的UART端口号
static QueueHandle_t g_sim_uart_queue = NULL; ///< 模拟器UART事件队列
static TaskHandle_t g_sim_uart_event_task_handle = NULL; ///< 模拟器UART事件处理任务句柄
static TaskHandle_t g_sim_ranging_data_task_handle = NULL; ///< 模拟器测距数据生成任务句柄

static int g_sim_actual_rx_buffer_size = 1024; ///< 模拟器UART实际接收缓冲区大小，会被配置覆盖

// UWB模块默认参数 (尽可能与数据手册中的出厂默认值保持一致)
static uwb_role_t g_sim_role = UWB_ROLE_MASTER;         ///< 模拟的角色: AT+ROLE=1 (主机)
static uint16_t g_sim_maddr = 0x0001;                   ///< 模拟的主机地址/从机本机地址: AT+MADDR=0001
static uint16_t g_sim_saddr0 = 0x0000;                  ///< 模拟的从机0地址/从机的主机地址: AT+SADDR0=0000
static uint16_t g_sim_saddr1 = 0x0000;                  ///< 模拟的从机1地址: AT+SADDR1=0000
static uint16_t g_sim_saddr2 = 0x0000;                  ///< 模拟的从机2地址: AT+SADDR2=0000
static uint8_t  g_sim_pid = UWB_DEFAULT_NETWORK_ID;   ///< 模拟的网络ID: AT+PID=255
static uint8_t  g_sim_period_factor = UWB_DEFAULT_PERIOD; ///< 模拟的测距周期因子: AT+PERIOD=100 (实际周期 = 值 * 10ms)
static uwb_lpwr_t g_sim_lpwr = UWB_LPWR_OFF;          ///< 模拟的低功耗模式: AT+LPWR=0 (关闭)
static uwb_mode_t g_sim_current_mode = UWB_MODE_RANGING; ///< 模拟的当前工作模式: 上电默认为测距模式
static uint8_t  g_sim_power_level = SIM_DEFAULT_POWER_LEVEL; ///< 模拟的功率等级: AT+PWR=3 (14dBm)
static int      g_sim_baud_rate = UWB_DEFAULT_BAUD_RATE; ///< 模拟的串口波特率: AT+UART=115200

static uint8_t g_sim_rx_line_buffer[SIM_RX_LINE_BUF_SIZE]; ///< 模拟器AT指令行接收缓冲区
static uint16_t g_sim_rx_line_pos = 0; ///< AT指令行接收缓冲区当前位置

// 内部任务函数前向声明
static void uwb_sim_uart_b_event_task(void *pvParameters);
static void uwb_sim_ranging_data_task(void *pvParameters);
static void sim_send_response(const char* response);


/**
 * @brief 将模拟器参数重置为出厂默认值。
 * @details 根据 uwb1claude.md 手册第六节 "出厂默认参数" 表格设置。
 */
static void sim_load_factory_defaults(void)
{
    ESP_LOGI(TAG_SIM, "正在加载模拟器出厂默认参数...");

    g_sim_role = UWB_ROLE_MASTER;         // 手册默认: 主机模式 (1)
    g_sim_power_level = SIM_DEFAULT_POWER_LEVEL; // 手册默认: 3 (14dBm)
    g_sim_pid = UWB_DEFAULT_NETWORK_ID;   // 手册默认: 255
    g_sim_period_factor = UWB_DEFAULT_PERIOD; // 手册默认: 100 (1s)
    // g_sim_baud_rate 在模拟器中于初始化时固定, 这里不改变其运行时值
    // 但其初始值 UWB_DEFAULT_BAUD_RATE (115200) 符合手册默认
    g_sim_lpwr = UWB_LPWR_OFF;          // 手册默认: 0 (关闭)
    g_sim_maddr = UWB_SLAVE_ADDR_0;     // 手册默认: 1 (主机地址)
    g_sim_saddr0 = UWB_MASTER_SELF_ADDR; // 手册默认: 0
    g_sim_saddr1 = UWB_MASTER_SELF_ADDR; // 手册默认: 0
    g_sim_saddr2 = UWB_MASTER_SELF_ADDR; // 手册默认: 0
    g_sim_current_mode = UWB_MODE_RANGING;  // 手册默认: 上电或复位后为测距模式

    ESP_LOGI(TAG_SIM, "模拟器出厂默认参数加载完成。当前模式: %s", g_sim_current_mode == UWB_MODE_RANGING ? "测距模式" : "AT指令模式");
}

// 通过模拟器UART发送响应字符串的辅助函数
static void sim_send_response(const char* response) 
{
    if (uart_is_driver_installed(g_sim_uart_port)) 
    {
        uart_write_bytes(g_sim_uart_port, response, strlen(response));
        ESP_LOGI(TAG_SIM, "发送模拟响应: %s", response);
    }
}

int uwb_simulator_init(const uwb_simulator_uart_config_t* sim_uart_config) {
    if (sim_uart_config == NULL) {
        return -1;
    }
    g_sim_uart_port = sim_uart_config->uart_num;
    g_sim_baud_rate = sim_uart_config->baud_rate; 
    g_sim_actual_rx_buffer_size = sim_uart_config->rx_buffer_size;

    ESP_LOGI(TAG_SIM, "初始化UWB模块模拟器 UART%d (TX:%d, RX:%d, 波特率:%d)",
             g_sim_uart_port, sim_uart_config->tx_pin, sim_uart_config->rx_pin, g_sim_baud_rate);

    uart_config_t uart_cfg = {
        .baud_rate = g_sim_baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t ret = uart_driver_install(g_sim_uart_port,
                                      sim_uart_config->rx_buffer_size,
                                      sim_uart_config->tx_buffer_size,
                                      20, &g_sim_uart_queue, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_SIM, "安装UART%d驱动失败: %s", g_sim_uart_port, esp_err_to_name(ret));
        return -1;
    }
    ret = uart_param_config(g_sim_uart_port, &uart_cfg);
    if (ret != ESP_OK) { ESP_LOGE(TAG_SIM, "配置UART%d参数失败", g_sim_uart_port); return -1; }
    ret = uart_set_pin(g_sim_uart_port, sim_uart_config->tx_pin, sim_uart_config->rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) { ESP_LOGE(TAG_SIM, "设置UART%d引脚失败", g_sim_uart_port); return -1; }

    uart_flush_input(g_sim_uart_port);
    vTaskDelay(pdMS_TO_TICKS(20)); 
    uart_flush_input(g_sim_uart_port);

    BaseType_t task_created;
    task_created = xTaskCreate(uwb_sim_uart_b_event_task, "sim_uart_event_task", 4096, NULL, 10, &g_sim_uart_event_task_handle);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG_SIM, "创建模拟器UART事件任务失败");
        uart_driver_delete(g_sim_uart_port);
        return -1;
    }

    task_created = xTaskCreate(uwb_sim_ranging_data_task, "sim_ranging_data_task", 2048, NULL, 5, &g_sim_ranging_data_task_handle);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG_SIM, "创建模拟测距数据任务失败");
        if(g_sim_uart_event_task_handle) vTaskDelete(g_sim_uart_event_task_handle);
        uart_driver_delete(g_sim_uart_port);
        return -1;
    }
    
    if (g_sim_current_mode == UWB_MODE_AT_COMMAND) {
         vTaskSuspend(g_sim_ranging_data_task_handle);
    } 

    ESP_LOGI(TAG_SIM, "UWB模拟器初始化完成。");
    return 0;
}

int uwb_simulator_deinit(void) {
    if (g_sim_ranging_data_task_handle != NULL) {
        vTaskDelete(g_sim_ranging_data_task_handle);
        g_sim_ranging_data_task_handle = NULL;
    }
    if (g_sim_uart_event_task_handle != NULL) {
        vTaskDelete(g_sim_uart_event_task_handle);
        g_sim_uart_event_task_handle = NULL;
    }
    if (uart_is_driver_installed(g_sim_uart_port)) {
        uart_driver_delete(g_sim_uart_port);
    }
    ESP_LOGI(TAG_SIM, "UWB模拟器已卸载");
    return 0;
}

static void uwb_sim_uart_b_event_task(void *pvParameters) 
{
    uart_event_t event;
    uint8_t* data = (uint8_t*) malloc(g_sim_actual_rx_buffer_size); 
    if (!data) {
        ESP_LOGE(TAG_SIM, "为模拟器UART事件任务分配接收缓冲区失败");
        vTaskDelete(NULL); 
        return;
    }
    
    ESP_LOGI(TAG_SIM, "模拟器UART事件任务已启动 (UART%d)", g_sim_uart_port);

    while(1){ 
        if (xQueueReceive(g_sim_uart_queue, (void *)&event, portMAX_DELAY)) 
        { 
            switch (event.type) 
            {
                case UART_DATA:
                    {
                        int len = uart_read_bytes(g_sim_uart_port, data, event.size, pdMS_TO_TICKS(100));
                        if (len > 0) 
                        {
                            ESP_LOGD(TAG_SIM, "UART%d 接收到 %d 字节数据:", g_sim_uart_port, len);
                            for (int i = 0; i < len; i++) 
                            { // 逐字节处理
                                uint8_t byte = data[i];
                                
                                // 记录每个字节和g_sim_rx_line_pos (Moved after a potential continue)
                                if (isprint(byte)) {
                                    ESP_LOGD(TAG_SIM, "Processing byte: 0x%02X ('%c'), g_sim_rx_line_pos: %d", byte, byte, g_sim_rx_line_pos);
                                } else {
                                    ESP_LOGD(TAG_SIM, "Processing byte: 0x%02X (NP), g_sim_rx_line_pos: %d", byte, g_sim_rx_line_pos);
                                }

                                if (byte == '\n') 
                                { // 遇到换行符，表示一行AT指令结束
                                    // 记录行结束前的缓冲区状态
                                    ESP_LOGD(TAG_SIM, "Newline detected. g_sim_rx_line_pos before modification: %d", g_sim_rx_line_pos);
                                    if (g_sim_rx_line_pos > 0) {
                                            //ESP_LOG_BUFFER_HEXDUMP(TAG_SIM, g_sim_rx_line_buffer, g_sim_rx_line_pos, ESP_LOG_DEBUG);
                                    }
                                    if (g_sim_rx_line_pos > 0 && g_sim_rx_line_buffer[g_sim_rx_line_pos - 1] == '\r') {
                                        g_sim_rx_line_buffer[g_sim_rx_line_pos - 1] = '\0'; // 去掉 \r
                                    } else {
                                        g_sim_rx_line_buffer[g_sim_rx_line_pos] = '\0';
                                    }
                                    const char* cmd_line = (const char*)g_sim_rx_line_buffer; // 当前处理的AT指令行
                                    ESP_LOGI(TAG_SIM, "接收到模拟AT指令: %s", cmd_line);
                                    ESP_LOGW(TAG_SIM, "<<< PARSED CMD_LINE: %s (len: %d)", cmd_line, strlen(cmd_line)); // 添加的日志行 (解析后)

                                    char response[256]; // AT指令响应缓冲区

                                    // 根据数据手册 (uwb1claude.md) 处理AT指令并生成响应
                                    if (strcmp(cmd_line, "AT+VER") == 0) {
                                        sprintf(response, "MK8000_SIM_V1.0\r\nOK\r\n");
                                    } 
                                    else if (strcmp(cmd_line, "AT+RST") == 0) {
                                        sprintf(response, "OK\r\n");
                                        // 模拟复位：模块进入测距模式，当前已配置的参数生效
                                        // 不再调用 sim_load_factory_defaults()，以保留已设置的参数
                                        g_sim_current_mode = UWB_MODE_RANGING; 
                                        ESP_LOGI(TAG_SIM, "模拟模块软件复位完成。当前配置已生效。模式: %s", g_sim_current_mode == UWB_MODE_RANGING ? "测距模式" : "AT指令模式");
                                        if(g_sim_ranging_data_task_handle && g_sim_current_mode == UWB_MODE_RANGING) {
                                             vTaskResume(g_sim_ranging_data_task_handle); 
                                             ESP_LOGI(TAG_SIM, "测距任务已恢复 (AT+RST)");
                                        } else if (g_sim_ranging_data_task_handle && g_sim_current_mode != UWB_MODE_RANGING) {
                                             ESP_LOGW(TAG_SIM, "AT+RST 后模式不为测距 (异常情况)，测距任务未恢复");
                                        }
                                    } 
                                    else if (strcmp(cmd_line, "AT+DEFT") == 0) {
                                         // 恢复出厂设置，与复位类似
                                        sprintf(response, "OK\r\n");
                                        sim_load_factory_defaults();
                                        ESP_LOGI(TAG_SIM, "模拟恢复出厂设置完成。已加载出厂默认参数。当前模式: %s", g_sim_current_mode == UWB_MODE_RANGING ? "测距模式" : "AT指令模式");
                                        if(g_sim_ranging_data_task_handle && g_sim_current_mode == UWB_MODE_RANGING) {
                                            vTaskResume(g_sim_ranging_data_task_handle); // 恢复测距任务
                                            ESP_LOGI(TAG_SIM, "测距任务已恢复 (AT+DEFT)");
                                        } else if (g_sim_ranging_data_task_handle && g_sim_current_mode != UWB_MODE_RANGING) {
                                            ESP_LOGW(TAG_SIM, "AT+DEFT 后模式不为测距，测距任务未恢复");
                                        }
                                    } 
                                    else if (strncmp(cmd_line, "AT+MODE=", 8) == 0) {
                                        int mode = atoi(cmd_line + 8); // 解析模式值
                                        if (mode == 0 || mode == 1) { // 0: AT指令模式, 1: 测距模式
                                            g_sim_current_mode = (uwb_mode_t)mode;
                                            sprintf(response, "OK\r\n");
                                            ESP_LOGI(TAG_SIM, "模拟器模式设置为: %s", g_sim_current_mode == UWB_MODE_RANGING ? "测距模式" : "AT指令模式");
                                            if (g_sim_current_mode == UWB_MODE_RANGING && g_sim_ranging_data_task_handle) {
                                                vTaskResume(g_sim_ranging_data_task_handle); // 进入测距模式，恢复测距任务
                                            } else if (g_sim_current_mode == UWB_MODE_AT_COMMAND && g_sim_ranging_data_task_handle) {
                                                vTaskSuspend(g_sim_ranging_data_task_handle); // 进入AT模式，挂起测距任务
                                            }
                                        } else {
                                            sprintf(response, "ERROR\r\n"); // 无效的模式值
                                        }
                                    } 
                                    else if (strcmp(cmd_line, "AT+MODE?") == 0) {
                                        // 查询模式可设置值范围
                                        sprintf(response, "MODE:0~1\r\n");
                                    } 
                                    else if (strcmp(cmd_line, "AT+MODE=?") == 0) {
                                        // 查询当前模式值
                                        sprintf(response, "AT+MODE=%d\r\n", g_sim_current_mode);
                                    }
                                    else if (strncmp(cmd_line, "AT+ROLE=", 8) == 0) {
                                        g_sim_role = (uwb_role_t)atoi(cmd_line + 8);
                                        sprintf(response, "OK\r\n");
                                    } 
                                    else if (strcmp(cmd_line, "AT+ROLE?") == 0) {
                                        // 查询角色可设置值范围
                                        sprintf(response, "ROLE:0~1\r\n");
                                    } 
                                    else if (strcmp(cmd_line, "AT+ROLE=?") == 0) {
                                        // 查询当前角色值
                                        sprintf(response, "AT+ROLE=%d\r\n", g_sim_role);
                                    } 
                                    else if (strncmp(cmd_line, "AT+PID=", 7) == 0) {
                                        g_sim_pid = atoi(cmd_line + 7);
                                        sprintf(response, "OK\r\n");
                                    } 
                                    else if (strcmp(cmd_line, "AT+PID?") == 0) {
                                        // 查询网络ID可设置值范围
                                        sprintf(response, "PID:0~255\r\n");
                                    } 
                                    else if (strcmp(cmd_line, "AT+PID=?") == 0) {
                                        // 查询当前网络ID值
                                        sprintf(response, "AT+PID=%d\r\n", g_sim_pid);
                                    } 
                                    else if (strncmp(cmd_line, "AT+PERIOD=", 10) == 0) {
                                        g_sim_period_factor = atoi(cmd_line + 10);
                                        sprintf(response, "OK\r\n");
                                    } 
                                    else if (strcmp(cmd_line, "AT+PERIOD?") == 0) {
                                        // 查询测距周期可设置值范围
                                        sprintf(response, "PERIOD:5~100\r\n");
                                    } 
                                    else if (strcmp(cmd_line, "AT+PERIOD=?") == 0) {
                                        // 查询当前测距周期值
                                        sprintf(response, "AT+PERIOD=%d\r\n", g_sim_period_factor);
                                    } 
                                    else if (strncmp(cmd_line, "AT+MADDR=", 9) == 0) {
                                        sscanf(cmd_line + 9, "%hx", &g_sim_maddr); // 解析十六进制地址
                                        sprintf(response, "OK\r\n");
                                    } 
                                    else if (strcmp(cmd_line, "AT+MADDR?") == 0) {
                                        // 查询主机地址可设置值范围
                                        sprintf(response, "MADDR:0~FFFF\r\n");
                                    } 
                                    else if (strncmp(cmd_line, "AT+SADDR0=", 10) == 0) {
                                        char addr_param_s0[5] = {0};
                                        strncpy(addr_param_s0, cmd_line + 10, 4); // Extract address parameter
                                        ESP_LOGI(TAG_SIM, "AT+SADDR0: Received param string '%s'", addr_param_s0);
                                        int parsed_s0 = sscanf(cmd_line + 10, "%hx", &g_sim_saddr0);
                                        ESP_LOGI(TAG_SIM, "AT+SADDR0: sscanf result %d, g_sim_saddr0 set to 0x%04X", parsed_s0, g_sim_saddr0);
                                        sprintf(response, "OK\r\n");
                                    } 
                                    else if (strcmp(cmd_line, "AT+SADDR0?") == 0) {
                                        // 查询从机0地址可设置值范围
                                        sprintf(response, "SADDR0:0~FFFF\r\n");
                                    } 
                                    else if (strncmp(cmd_line, "AT+SADDR1=", 10) == 0) {
                                        char addr_param_s1[5] = {0};
                                        strncpy(addr_param_s1, cmd_line + 10, 4); // Extract address parameter
                                        ESP_LOGI(TAG_SIM, "AT+SADDR1: Received param string '%s'", addr_param_s1);
                                        int parsed_s1 = sscanf(cmd_line + 10, "%hx", &g_sim_saddr1);
                                        ESP_LOGI(TAG_SIM, "AT+SADDR1: sscanf result %d, g_sim_saddr1 set to 0x%04X", parsed_s1, g_sim_saddr1);
                                        sprintf(response, "OK\r\n");
                                    } 
                                    else if (strcmp(cmd_line, "AT+SADDR1?") == 0) {
                                        // 查询从机1地址可设置值范围
                                        sprintf(response, "SADDR1:0~FFFF\r\n");
                                    } 
                                    else if (strncmp(cmd_line, "AT+SADDR2=", 10) == 0) {
                                        char addr_param_s2[5] = {0};
                                        strncpy(addr_param_s2, cmd_line + 10, 4); // Extract address parameter
                                        ESP_LOGI(TAG_SIM, "AT+SADDR2: Received param string '%s'", addr_param_s2);
                                        int parsed_s2 = sscanf(cmd_line + 10, "%hx", &g_sim_saddr2);
                                        ESP_LOGI(TAG_SIM, "AT+SADDR2: sscanf result %d, g_sim_saddr2 set to 0x%04X", parsed_s2, g_sim_saddr2);
                                        sprintf(response, "OK\r\n");
                                    } 
                                    else if (strcmp(cmd_line, "AT+SADDR2?") == 0) {
                                        // 查询从机2地址可设置值范围
                                        sprintf(response, "SADDR2:0~FFFF\r\n");
                                    } 
                                    else if (strncmp(cmd_line, "AT+LPWR=", 8) == 0) {
                                        g_sim_lpwr = (uwb_lpwr_t)atoi(cmd_line + 8);
                                        sprintf(response, "OK\r\n");
                                    }
                                    else if (strcmp(cmd_line, "AT+LPWR?") == 0) {
                                        // 查询低功耗模式可设置值范围
                                        sprintf(response, "LPWR:0~1\r\n");
                                    } 
                                    else if (strncmp(cmd_line, "AT+PWR=", 7) == 0) {
                                        g_sim_power_level = atoi(cmd_line + 7);
                                        sprintf(response, "OK\r\n");
                                    } 
                                    else if (strcmp(cmd_line, "AT+PWR?") == 0) {
                                        // 查询功率等级可设置值范围
                                        sprintf(response, "PWR:0~4\r\n");
                                    } 
                                    else if (strncmp(cmd_line, "AT+UART=",8) == 0) {
                                        // 注意: 模拟器的实际UART波特率在初始化时固定。
                                        // 此命令可以被应答，但不会改变模拟器硬件的波特率。
                                        sprintf(response, "OK\r\n"); 
                                    } 
                                    else if (strcmp(cmd_line, "AT+UART?") == 0) {
                                        // 查询波特率可设置值范围
                                        sprintf(response, "BAUD:115200\r\n");
                                    } 
                                    else if (strcmp(cmd_line, "AT+ALL") == 0) { // 查询所有参数
                                        char all_buf[200]; // 临时缓冲区用于构建AT+ALL的响应
                                        sprintf(all_buf, "AT+ROLE=%d\r\nAT+PWR=%d\r\nAT+PID=%d\r\nAT+PERIOD=%d\r\nAT+UART=%d\r\nAT+LPWR=%d\r\nAT+MADDR=%04X\r\nAT+SADDR0=%04X\r\nAT+SADDR1=%04X\r\nAT+SADDR2=%04X\r\nOK\r\n",
                                            g_sim_role, g_sim_power_level, g_sim_pid, g_sim_period_factor, g_sim_baud_rate, g_sim_lpwr, g_sim_maddr, g_sim_saddr0, g_sim_saddr1, g_sim_saddr2);
                                        strcpy(response, all_buf);
                                    } 
                                    else {
                                        ESP_LOGW(TAG_SIM, "未知的模拟AT指令: %s", cmd_line);
                                        sprintf(response, "ERROR\r\n"); // 对于无法识别的指令，回复ERROR
                                    }

                                    sim_send_response(response); // ***发送AT响应***

                                    g_sim_rx_line_pos = 0; // 重置行缓冲区，准备接收下一行

                                } 
                                else if (g_sim_rx_line_pos < SIM_RX_LINE_BUF_SIZE - 1) {
                                    g_sim_rx_line_buffer[g_sim_rx_line_pos++] = byte; // 字节存入行缓冲区
                                } 
                                else {
                                    ESP_LOGW(TAG_SIM, "模拟器接收行缓冲区溢出");
                                    g_sim_rx_line_pos = 0; // 溢出则重置缓冲区
                                }
                            }
                        } // if (len > 0)
                    } // UART_DATA
                    break;
                case UART_FIFO_OVF: 
                    ESP_LOGW(TAG_SIM, "UART%d 接收FIFO溢出", g_sim_uart_port); 
                    uart_flush_input(g_sim_uart_port); 
                    xQueueReset(g_sim_uart_queue); 
                    break;
                case UART_BUFFER_FULL: 
                    ESP_LOGW(TAG_SIM, "UART%d 接收环形缓冲区满", g_sim_uart_port); 
                    uart_flush_input(g_sim_uart_port); 
                    xQueueReset(g_sim_uart_queue); 
                    break;
                default:
                    ESP_LOGD(TAG_SIM, "UART%d 事件: %d", g_sim_uart_port, event.type);
                    break;
            }
        }
    }
    free(data); // 释放接收缓冲区内存
    vTaskDelete(NULL); // 删除任务自身
}

static void uwb_sim_ranging_data_task(void *pvParameters) 
{
    uint16_t dist_cm = SIM_DEFAULT_DIST_MIN; // 初始模拟距离
    uint16_t current_target_slave_addr; 
    int slave_idx = 0; // 用于轮询从机地址
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG_SIM, "模拟器测距数据任务已启动。");

    for (;;) { // 无限循环生成数据
        // 根据配置的周期因子计算延时 (单位:毫秒)
        uint32_t delay_ms = (g_sim_period_factor > 0 ? g_sim_period_factor : 100) * 10; // 如果因子为0，则默认为1秒 (100*10ms)
        vTaskDelay(pdMS_TO_TICKS(delay_ms));

        if (g_sim_current_mode == UWB_MODE_RANGING) { // 仅在测距模式下发送数据
            uint8_t frame[SIM_RANGING_FRAME_SIZE]; // 测距数据帧缓冲区
            frame[0] = 0xF0; // 帧头
            frame[1] = 0x05; // 数据长度 (固定为5字节: 地址2 + 距离2 + RSSI1)

            // 根据模拟器角色决定发送方地址
            if (g_sim_role == UWB_ROLE_MASTER) {
                // 主机模式下，模拟器发送的数据帧中的"发送方地址"应为某个从机的地址
                // 真实主机是接收来自从机的数据。
                // 为模拟此行为，轮流使用配置的SADDR0, SADDR1, SADDR2 
                uint16_t slaves[3] = {g_sim_saddr0, g_sim_saddr1, g_sim_saddr2};
                do {
                    current_target_slave_addr = slaves[slave_idx % 3];
                    slave_idx++;
                } while (current_target_slave_addr == 0 && slave_idx < 6); // 避免在所有从机地址都为0时死循环
                if(current_target_slave_addr == 0) current_target_slave_addr = 0x00A1; // 如果没有配置从机地址，使用一个备用地址

                frame[2] = current_target_slave_addr & 0xFF;      // 发送方地址 低字节 (模拟从机地址)
                frame[3] = (current_target_slave_addr >> 8) & 0xFF; // 发送方地址 高字节
            } else { // UWB_ROLE_SLAVE (从机模式)
                // 从机模式下，发送方地址是其自身的 MADDR (在数据手册中，从机的本机地址通过AT+MADDR设置)
                frame[2] = g_sim_maddr & 0xFF;      // 发送方地址 低字节 (模拟从机自身地址)
                frame[3] = (g_sim_maddr >> 8) & 0xFF; // 发送方地址 高字节
            }

            dist_cm = SIM_DEFAULT_DIST_MIN + (rand() % SIM_DEFAULT_DIST_RANGE); // 随机生成距离: 50-249 cm
            frame[4] = dist_cm & 0xFF;        // 距离 低字节
            frame[5] = (dist_cm >> 8) & 0xFF; // 距离 高字节

            int8_t rssi_val = SIM_DEFAULT_RSSI_BASE - (rand() % SIM_DEFAULT_RSSI_RANGE); // 随机生成RSSI: -50 到 -89 dBm
            frame[6] = (uint8_t)(rssi_val + 256); // RSSI 存储值 = 实际值 + 256
            frame[7] = 0xAA; // 帧尾

            if (uart_is_driver_installed(g_sim_uart_port)) {
                 ESP_LOGD(TAG_SIM, "模拟器: 发送测距帧. 地址:0x%04X, 距离:%dcm, RSSI:%ddBm (原始值:0x%02X)", 
                    (uint16_t)(frame[3] << 8 | frame[2]), dist_cm, rssi_val, frame[6]);
                uart_write_bytes(g_sim_uart_port, (const char*)frame, SIM_RANGING_FRAME_SIZE);
            }
        } else {
            ESP_LOGD(TAG_SIM, "模拟器测距任务: 当前非测距模式或任务已挂起。");
        }
    }
} 