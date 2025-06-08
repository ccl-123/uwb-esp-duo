/**
 * @file uwb_core.c
 * @brief UWB 模块驱动核心逻辑实现 (重构)
 * @details 实现了公共 API 函数，管理模块状态，并协调 AT 指令处理。
 *          包含了数据接收、缓冲、解析和分发的逻辑。
 * @date 2025-06-07
 */

#include "../include/uwb_mk8000.h"
#include <string.h>
#include <stdio.h>

/* ------------------------- 内部定义 ------------------------- */
#define RX_LINE_BUF_SIZE 512 ///< 行缓冲区大小
#define RANGING_FRAME_SIZE 8 ///< 测距数据帧固定大小

/* ------------------------- 内部变量 ------------------------- */
static uwb_ranging_callback_t g_ranging_cb = NULL; ///< 测距回调函数
static bool g_driver_initialized = false;          ///< 驱动是否初始化标志
static uint8_t g_rx_line_buffer[RX_LINE_BUF_SIZE]; ///< 接收行缓冲区
static uint16_t g_rx_line_pos = 0;                 ///< 行缓冲区当前位置

/* ------------------------- 引用外部函数 (来自 uwb_at.c) ------------------------- */
extern int  uwb_at_init(void);
extern void uwb_at_deinit(void);
extern int  uwb_at_send_cmd_sync(const char* cmd, char* response_buf, size_t buf_len, uint32_t timeout_ms);
extern void uwb_at_handle_response_line(const char* line);

/* ------------------------- 内部函数 ------------------------- */

/**
 * @brief 解析并处理测距数据帧。
 * @param[in] frame 数据帧指针。
 * @param[in] len   数据帧长度。
 */
static void handle_ranging_frame(const uint8_t* frame, uint16_t len)
{
    // 根据数据手册, 帧格式为 F0 05 AddrL AddrH DistL DistH Rssi AA
    if (len == RANGING_FRAME_SIZE && frame[0] == 0xF0 && frame[1] == 0x05 && frame[7] == 0xAA) {
        uwb_ranging_data_t data;
        data.sender_address = frame[2] | (frame[3] << 8);
        data.distance_cm = frame[4] | (frame[5] << 8);
        data.rssi_dbm = (int8_t)(frame[6] - 256); // RSSI = 值 - 256

        HAL_LOGD("Parsed Ranging: Addr=0x%04X, Dist=%u, RSSI=%d", data.sender_address, data.distance_cm, data.rssi_dbm);

        if (g_ranging_cb) {
            g_ranging_cb(&data);
        }
    } else {
        HAL_LOGW("Invalid ranging frame detected.");
    }
}

/**
 * @brief 将地址转换为 4 位十六进制字符串。
 * @param[in] addr 地址。
 * @param[out] hex_str 输出的字符串缓冲区 (至少 5 字节)。
 */
static void addr_to_hex_str(uint16_t addr, char* hex_str)
{
    sprintf(hex_str, "%04X", addr);
}

/* ------------------------- 公共API实现 ------------------------- */

int uwb_driver_init(uwb_ranging_callback_t ranging_cb)
{
    if (g_driver_initialized) {
        HAL_LOGW("Driver already initialized.");
        return 0;
    }
    if (ranging_cb == NULL) {
        HAL_LOGE("Ranging callback cannot be NULL.");
        return -1;
    }

    g_ranging_cb = ranging_cb;

    if (uwb_at_init() != 0) {
        return -1;
    }

    g_driver_initialized = true;
    HAL_LOGI("UWB driver core logic initialized.");
    return 0;
}

int uwb_driver_deinit(void)
{
    if (!g_driver_initialized) return 0;

    uwb_at_deinit();
    g_ranging_cb = NULL;
    g_driver_initialized = false;
    HAL_LOGI("UWB driver core logic deinitialized.");
    return 0;
}

void uwb_driver_process_data(const uint8_t* data, size_t len)
{
    for (uint16_t i = 0; i < len; i++) 
    {
        uint8_t byte = data[i];

        // 尝试检测测距帧头 F0
        if (byte == 0xF0 && g_rx_line_pos == 0) { 
            g_rx_line_buffer[0] = 0xF0;
            g_rx_line_pos = 1;
            continue;
        }

        // 如果正在接收测距帧
        if (g_rx_line_pos > 0 && g_rx_line_buffer[0] == 0xF0) 
        {
            g_rx_line_buffer[g_rx_line_pos++] = byte;
            if (g_rx_line_pos == RANGING_FRAME_SIZE) {
                handle_ranging_frame(g_rx_line_buffer, RANGING_FRAME_SIZE);
                g_rx_line_pos = 0; // 重置缓冲区
            } 
            else if (g_rx_line_pos >= RX_LINE_BUF_SIZE) {
                HAL_LOGW("Ranging frame buffer overflow, resetting.");
                g_rx_line_pos = 0; // 缓冲区溢出，重置
            }
            continue;
        }

        // 否则，假设是 AT 响应，按行处理
        if (byte == '\n') {
            if (g_rx_line_pos < RX_LINE_BUF_SIZE) {
                if (g_rx_line_pos > 0 && g_rx_line_buffer[g_rx_line_pos - 1] == '\r') {
                    g_rx_line_buffer[g_rx_line_pos - 1] = '\0'; // 去掉 \r
                } else {
                    g_rx_line_buffer[g_rx_line_pos] = '\0';
                }
                uwb_at_handle_response_line((const char*)g_rx_line_buffer);
            } else {
                g_rx_line_buffer[RX_LINE_BUF_SIZE - 1] = '\0';
                HAL_LOGW("Line buffer full, truncating to %d bytes", RX_LINE_BUF_SIZE - 1);
                uwb_at_handle_response_line((const char*)g_rx_line_buffer);
            }
            
            g_rx_line_pos = 0; // 重置行缓冲区
        } else if (g_rx_line_pos < RX_LINE_BUF_SIZE - 1) {
            g_rx_line_buffer[g_rx_line_pos++] = byte;
        }
    }
}

int uwb_set_work_mode(uwb_mode_t mode)
{
    if (!g_driver_initialized) return -1;
    char cmd[20];
    sprintf(cmd, "AT+MODE=%d\r\n", mode);
    return uwb_at_send_cmd_sync(cmd, NULL, 0, AT_CMD_TIMEOUT_MS);
}

int uwb_configure_module(const uwb_settings_t* settings)
{
    if (!g_driver_initialized || settings == NULL) return -1;

    char cmd[32];
    char addr_str[5];

    HAL_LOGI("Starting UWB module configuration...");

    // 1. 进入 AT 模式 (多次重试)
    int retries = 0;
    while (uwb_set_work_mode(UWB_MODE_AT_COMMAND) != 0) {
        retries++;
        if (retries >= UWB_ENTER_AT_RETRIES) {
            HAL_LOGE("Failed to enter AT mode after %d attempts. Aborting configuration.", retries);
            return -1;
        }
        HAL_LOGW("Initial attempt to enter AT mode failed. Retrying... (%d/%d)", retries, UWB_ENTER_AT_RETRIES);
        hal_delay_ms(100); // 增加延时以等待模块稳定
    }
    
    hal_delay_ms(100); // 给模块一点时间

    // 2. 配置角色
    sprintf(cmd, "AT+ROLE=%d\r\n", settings->role);
    if (uwb_at_send_cmd_sync(cmd, NULL, 0, AT_CMD_TIMEOUT_MS) != 0) goto config_error;

    // 3. 配置网络 ID
    sprintf(cmd, "AT+PID=%d\r\n", settings->network_id);
    if (uwb_at_send_cmd_sync(cmd, NULL, 0, AT_CMD_TIMEOUT_MS) != 0) goto config_error;

    // 4. 配置周期
    sprintf(cmd, "AT+PERIOD=%d\r\n", settings->ranging_period);
    if (uwb_at_send_cmd_sync(cmd, NULL, 0, AT_CMD_TIMEOUT_MS) != 0) goto config_error;

    // 5. 配置地址 (根据角色)
    if (settings->role == UWB_ROLE_SLAVE) {
        addr_to_hex_str(settings->self_address, addr_str);
        sprintf(cmd, "AT+MADDR=%s\r\n", addr_str); //  从机模式下, MADDR 是本机地址
        if (uwb_at_send_cmd_sync(cmd, NULL, 0, AT_CMD_TIMEOUT_MS) != 0) goto config_error;

        addr_to_hex_str(settings->master_address, addr_str);
        sprintf(cmd, "AT+SADDR0=%s\r\n", addr_str); // 从机模式下, SADDR0 是主机地址
        if (uwb_at_send_cmd_sync(cmd, NULL, 0, AT_CMD_TIMEOUT_MS) != 0) goto config_error;
    } else { // UWB_ROLE_MASTER
        addr_to_hex_str(settings->self_address, addr_str);
        sprintf(cmd, "AT+MADDR=%s\r\n", addr_str); // 主机模式下, MADDR 是本机地址
        if (uwb_at_send_cmd_sync(cmd, NULL, 0, AT_CMD_TIMEOUT_MS) != 0) goto config_error;

        addr_to_hex_str(settings->slave_addr_0, addr_str);
        sprintf(cmd, "AT+SADDR0=%s\r\n", addr_str); 
        if (uwb_at_send_cmd_sync(cmd, NULL, 0, AT_CMD_TIMEOUT_MS) != 0) goto config_error;

        addr_to_hex_str(settings->slave_addr_1, addr_str);
        sprintf(cmd, "AT+SADDR1=%s\r\n", addr_str);
        if (uwb_at_send_cmd_sync(cmd, NULL, 0, AT_CMD_TIMEOUT_MS) != 0) goto config_error;

        addr_to_hex_str(settings->slave_addr_2, addr_str);
        sprintf(cmd, "AT+SADDR2=%s\r\n", addr_str); 
        if (uwb_at_send_cmd_sync(cmd, NULL, 0, AT_CMD_TIMEOUT_MS) != 0) goto config_error;
    }

    // 6. 配置低功耗模式
    sprintf(cmd, "AT+LPWR=%d\r\n", settings->low_power_mode); 
    if (uwb_at_send_cmd_sync(cmd, NULL, 0, AT_CMD_TIMEOUT_MS) != 0) goto config_error;

    // 7. 复位模块使配置生效
    HAL_LOGI("Configuration sent, resetting module...");
    if (uwb_software_reset() != 0) {
        HAL_LOGE("Failed to reset module.");
        goto config_error;
    }
    hal_delay_ms(1500); // 等待模块复位完成

    HAL_LOGI("UWB module configuration completed.");
    return 0;

config_error:
    HAL_LOGE("Configuration failed! Attempting to revert to RANGING mode.");
    uwb_set_work_mode(UWB_MODE_RANGING); // 尝试恢复到测距模式
    return -1;
}

int uwb_software_reset(void)
{
    if (!g_driver_initialized) return -1;
    return uwb_at_send_cmd_sync("AT+RST\r\n", NULL, 0, 1500);
}

int uwb_factory_reset(void)
{
    if (!g_driver_initialized) return -1;
    return uwb_at_send_cmd_sync("AT+DEFT\r\n", NULL, 0, 1500); 
}

int uwb_query_version(char* buffer, size_t buffer_len)
{
    if (!g_driver_initialized) return -1;
    return uwb_at_send_cmd_sync("AT+VER\r\n", buffer, buffer_len, AT_CMD_TIMEOUT_MS); 
}

int uwb_query_all_params(char* buffer, size_t buffer_len)
{
    if (!g_driver_initialized) return -1;
    return uwb_at_send_cmd_sync("AT+ALL\r\n", buffer, buffer_len, AT_CMD_TIMEOUT_MS); 
}
