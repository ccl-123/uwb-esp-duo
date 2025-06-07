/**
 * @file main.c
 * @brief UWB 驱动在 ESP32 平台上的应用示例
 * @details 演示如何在 ESP-IDF 环境中初始化 HAL 和 UWB 驱动，配置模块，
 *          以及如何创建一个 FreeRTOS 任务来接收和处理 UWB 数据。
 * @date 2025-06-08
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "uwb_mk8000.h" 
#include "uwb_hal.h"    

// --- 平台专属配置 ---
#include "hal_esp32_config.h"

// --- 仿真模式 ---
#ifdef UWB_DUAL_UART_SIM_MODE
#include "uwb_module_simulator.h" 
#endif

// HACK: 直接引用内部函数用于测试
extern int uwb_at_send_cmd_sync(const char* cmd, char* response_buf, size_t buf_len, uint32_t timeout_ms);

static const char* TAG = "UWB_APP_ESP32";

/**
 * @brief 测距数据回调处理函数
 */
void ranging_data_handler(const uwb_ranging_data_t* data)
{
    if (data) {
        ESP_LOGI(TAG, "Ranging Data: Addr=0x%04X, Dist=%u cm, RSSI=%d dBm",
                 data->sender_address,
                 data->distance_cm,
                 data->rssi_dbm);
    }
}

/**
 * @brief 测试UWB查询命令功能
 * @details 测试多种AT查询命令以验证驱动和模块的响应。
 */
void test_uwb_query_commands(void)
{
    int ret;
    char buffer[512] = {0};
    
    ESP_LOGI(TAG, "===== 开始UWB查询命令测试 =====");
    
    // 1. 切换到AT命令模式
    ESP_LOGI(TAG, "切换到AT指令模式...");
    ret = uwb_set_work_mode(UWB_MODE_AT_COMMAND);
    if (ret != 0) {
        ESP_LOGE(TAG, "设置AT命令模式失败");
        // 即使失败也尝试恢复，避免模块停在未知状态
        uwb_set_work_mode(UWB_MODE_RANGING);
        return;
    }
    
    // 等待模式切换完成
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // 2. 测试AT+VER查询
    ESP_LOGI(TAG, "【测试1】查询固件版本 AT+VER");
    memset(buffer, 0, sizeof(buffer));
    ret = uwb_query_version(buffer, sizeof(buffer));
    if (ret == 0) {
        ESP_LOGI(TAG, "版本查询成功：\n%s", buffer);
    } else {
        ESP_LOGE(TAG, "版本查询失败");
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // 3. 测试AT+ALL查询
    ESP_LOGI(TAG, "【测试2】查询所有参数 AT+ALL");
    memset(buffer, 0, sizeof(buffer));
    ret = uwb_query_all_params(buffer, sizeof(buffer));
    if (ret == 0) {
        ESP_LOGI(TAG, "全部参数查询成功：\n%s", buffer);
    } else {
        ESP_LOGE(TAG, "全部参数查询失败");
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // 4. 测试AT+ROLE?查询
    ESP_LOGI(TAG, "【测试3】查询角色参数范围 AT+ROLE?");
    memset(buffer, 0, sizeof(buffer));
    ret = uwb_at_send_cmd_sync("AT+ROLE?\r\n", buffer, sizeof(buffer), 500);
    if (ret == 0) {
        ESP_LOGI(TAG, "角色参数范围查询成功：\n%s", buffer);
    } else {
        ESP_LOGE(TAG, "角色参数范围查询失败");
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // 5. 测试AT+PERIOD?查询
    ESP_LOGI(TAG, "【测试4】查询测距周期参数范围 AT+PERIOD?");
    memset(buffer, 0, sizeof(buffer));
    ret = uwb_at_send_cmd_sync("AT+PERIOD?\r\n", buffer, sizeof(buffer), 500);
    if (ret == 0) {
        ESP_LOGI(TAG, "测距周期参数范围查询成功：\n%s", buffer);
    } else {
        ESP_LOGE(TAG, "测距周期参数范围查询失败");
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    // 6. 恢复测距模式
    ESP_LOGI(TAG, "恢复测距模式...");
    ret = uwb_set_work_mode(UWB_MODE_RANGING);
    if (ret != 0) {
        ESP_LOGE(TAG, "恢复测距模式失败");
    }
    
    ESP_LOGI(TAG, "===== UWB查询命令测试完成 =====");
}

/**
 * @brief UWB 数据接收任务
 * @details 该任务在一个循环中持续从串口读取数据，并将其喂给 UWB 驱动核心进行处理。
 */
void uwb_receive_task(void* pvParameters)
{
    (void)pvParameters;

    uint8_t* rx_buffer = (uint8_t*) malloc(UWB_BUFFER_SIZE);
    if (rx_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for RX buffer");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "UWB receive task started.");

    while (1) {
        // 尝试从串口读取数据，超时时间设置为 200ms
        int len = hal_uart_read(rx_buffer, UWB_BUFFER_SIZE, 200);
        if (len > 0) {
            // 如果读到数据，就喂给驱动核心
            uwb_driver_process_data(rx_buffer, len);
        }
    }

    free(rx_buffer);
    vTaskDelete(NULL);
}

/**
 * @brief 主应用程序入口
 */
void app_main(void)
{
    // 初始化 NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "--- UWB MK8000 Driver Example for ESP32 ---");

    // --- 如果定义了仿真模式，则初始化仿真器 ---
#ifdef UWB_DUAL_UART_SIM_MODE
    ESP_LOGI(TAG, "Dual UART Simulation Mode is ENABLED.");
    
    uwb_simulator_uart_config_t sim_uart_config = {
        .uart_num = SIM_UART_NUM,
        .tx_pin = UWB_SIM_UART_TX_PIN,
        .rx_pin = UWB_SIM_UART_RX_PIN,
        .baud_rate = UWB_DEFAULT_BAUD_RATE,
        .rx_buffer_size = UWB_BUFFER_SIZE * 2,
        .tx_buffer_size = UWB_BUFFER_SIZE
    };
    if (uwb_simulator_init(&sim_uart_config) != 0) {
        ESP_LOGE(TAG, "Failed to initialize UWB module simulator!");
        return;
    }
    ESP_LOGI(TAG, "UWB module simulator initialized on UART%d.", sim_uart_config.uart_num);
    vTaskDelay(pdMS_TO_TICKS(100)); 
#endif

    // 1. 定义 HAL 层的 UART 配置
    char uart_num_str[4];
    sprintf(uart_num_str, "%d", UWB_UART_NUM);

    uwb_hal_uart_config_t uart_config = {
        .device_name = uart_num_str,
        .baud_rate = UWB_DEFAULT_BAUD_RATE,
        .rx_buffer_size = UWB_BUFFER_SIZE * 2,
        .tx_buffer_size = UWB_BUFFER_SIZE,
        .tx_pin = UWB_UART_TX_PIN,
        .rx_pin = UWB_UART_RX_PIN,
    };

    // 2. 初始化 HAL 层的 UART
    if (hal_uart_init(&uart_config) != 0) {
        ESP_LOGE(TAG, "Failed to initialize HAL UART.");
        return;
    }

    // 3. 初始化 UWB 驱动核心逻辑
    if (uwb_driver_init(ranging_data_handler) != 0) {
        ESP_LOGE(TAG, "Failed to initialize UWB driver.");
        hal_uart_deinit();
        return;
    }

    // 4. 创建并启动数据接收任务
    xTaskCreate(uwb_receive_task, "uwb_rx_task", 4096, NULL, 5, NULL);

    vTaskDelay(pdMS_TO_TICKS(100));

    // 4. 定义模块配置 (配置为"主机")
    uwb_settings_t my_settings = {
        .role = UWB_ROLE_MASTER,
        .self_address = UWB_MASTER_SELF_ADDR,
        .master_address = UWB_MASTER_SELF_ADDR,
        .slave_addr_0 = UWB_SLAVE_ADDR_0,
        .slave_addr_1 = UWB_SLAVE_ADDR_1,
        .slave_addr_2 = UWB_SLAVE_ADDR_2,
        .network_id = UWB_DEFAULT_NETWORK_ID,
        .ranging_period = UWB_RANGING_PERIOD,
        .low_power_mode = UWB_LPWR_OFF
    };

    // 5. 配置 UWB 模块
    ESP_LOGI(TAG, "Configuring UWB module...");
    if (uwb_configure_module(&my_settings) != 0) {
        ESP_LOGE(TAG, "Failed to configure UWB module!");
    } else {
        ESP_LOGI(TAG, "UWB module configured successfully. Waiting for ranging data...");
    }

    // 6. 运行UWB查询命令测试
    ESP_LOGI(TAG, "Running UWB query command tests after 3 seconds...");
    vTaskDelay(pdMS_TO_TICKS(3000)); // 等待3秒让模块在配置后稳定
    test_uwb_query_commands();

    // 7. 主任务循环
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        // 主任务可以留空或执行其他逻辑
    }
}
