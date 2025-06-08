/**
 * @file uwb_at.c
 * @brief UWB 模块 AT 指令的构建、发送和响应处理实现 
 * @details 提供发送 AT 指令并同步等待响应的机制。
 * @date 2025-06-07
 */

#include "../include/uwb_mk8000.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <semaphore.h>
#include <time.h>
#include <errno.h>

/* ------------------------- 内部定义 ------------------------- */
#define AT_RESPONSE_BUF_SIZE (512) ///< AT 响应缓冲区大小

/* ------------------------- 内部变量 ------------------------- */
static sem_t                g_at_response_sem;      ///< AT 响应同步信号量
static bool                 g_at_sem_initialized = false;
static char                 g_at_response_buffer[AT_RESPONSE_BUF_SIZE]; ///< AT 响应缓冲区
static bool                 g_at_response_ok = false; ///< AT 响应是否为 OK

/* ------------------------- 函数声明 ------------------------- */
int uwb_at_send_cmd_sync(const char* cmd, char* response_buf, size_t buf_len, uint32_t timeout_ms);
void uwb_at_handle_response_line(const char* line);
int uwb_at_init(void);
void uwb_at_deinit(void);

/* ------------------------- 内部API实现 ------------------------- */

/**
 * @brief 初始化 AT 指令处理模块。
 * @return int 0 表示成功, -1 表示失败
 */
int uwb_at_init(void)
{
    if (!g_at_sem_initialized) {
        if (sem_init(&g_at_response_sem, 0, 0) == -1) {
            HAL_LOGE("Failed to create AT response semaphore, error: %s", strerror(errno));
            return -1;
        }
        g_at_sem_initialized = true;
    }
    return 0;
}

/**
 * @brief 反初始化 AT 指令处理模块。
 */
void uwb_at_deinit(void)
{
    if (g_at_sem_initialized) {
        sem_destroy(&g_at_response_sem);
        g_at_sem_initialized = false;
    }
}

/**
 * @brief 发送 AT 指令并同步等待响应。
 * @param[in] cmd 要发送的 AT 指令字符串 (必须以 \r\n 结尾)。
 * @param[out] response_buf 存储响应的缓冲区 (可选, 可为 NULL)。
 * @param[in] buf_len 缓冲区大小。
 * @param[in] timeout_ms 等待响应的超时时间。
 * @return int 0 表示成功收到 "OK", -1 表示失败、超时或错误
 */
int uwb_at_send_cmd_sync(const char* cmd, char* response_buf, size_t buf_len, uint32_t timeout_ms)
{
    if (!g_at_sem_initialized) {
        HAL_LOGE("AT module not initialized.");
        return -1;
    }

    // 清空上次响应并排空信号量，以防上次有残留
    memset(g_at_response_buffer, 0, AT_RESPONSE_BUF_SIZE);
    while (sem_trywait(&g_at_response_sem) == 0); // Drain the semaphore

    HAL_LOGD(">>> SENDING AT CMD: %s", cmd);
    
    if (hal_uart_write((const uint8_t*)cmd, strlen(cmd)) <= 0) {
        HAL_LOGE("Failed to send AT command: %s", cmd);
        return -1;
    }

    // 等待响应信号
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
        HAL_LOGE("clock_gettime error: %s", strerror(errno));
        return -1;
    }
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000;
    // 处理纳秒进位
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }

    if (sem_timedwait(&g_at_response_sem, &ts) == 0) {
        // 收到了响应
        if (response_buf != NULL && buf_len > 0) {
            strncpy(response_buf, g_at_response_buffer, buf_len - 1);
            response_buf[buf_len - 1] = '\0';
        }
        return g_at_response_ok ? 0 : -1;
    } else {
        // 等待超时或错误
        if (errno == ETIMEDOUT) {
            HAL_LOGW("Timeout waiting for response for: %s", cmd);
        } else {
            HAL_LOGE("sem_timedwait error: %s", strerror(errno));
        }
        return -1;
    }
}

/**
 * @brief 处理接收到的 AT 响应行。
 * @note 此函数由 uwb_driver_process_data 调用。
 * @param[in] line 接收到的 AT 响应行 (不含 \r\n)。
 */
void uwb_at_handle_response_line(const char* line)
{
    if (line == NULL) return;

    HAL_LOGD("Handling AT line: [%s]", line);
    
    bool is_final_response = false;
    // 检查是否是最终成功或失败响应
    if (strcmp(line, "OK") == 0) {
        g_at_response_ok = true;
        is_final_response = true;
    } else if (strcmp(line, "ERROR") == 0) {
        g_at_response_ok = false;
        is_final_response = true;
    }

    // 检查是否为查询参数响应（格式为 XXX:value 或 XXX:range）
    // 根据数据手册，这类响应也表示指令被成功接收和处理
    static const char* query_prefixes[] = {
        "ROLE:", "UART:", "PID:", "PWR:", "LPWR:", 
        "MADDR:", "SADDR0:", "SADDR1:", "SADDR2:",
        "PERIOD:", "MODE:", "BAUD:", "VER:"
    };
    
    for (size_t i = 0; i < sizeof(query_prefixes) / sizeof(query_prefixes[0]); i++) {
        if (strncmp(line, query_prefixes[i], strlen(query_prefixes[i])) == 0) {
            g_at_response_ok = true;  // 查询响应被视为成功
            is_final_response = true; // 查询响应也被视为最终响应
            break;
        }
    }
    
    // 特殊处理：检查是否是 AT+ALL 响应的最后一行 (AT+SADDR2=...)
    // 这是因为 AT+ALL 会返回多行，我们需要一个明确的结束标志
    if (!is_final_response && strncmp(line, "AT+SADDR2=", 10) == 0) {
        is_final_response = true;
        g_at_response_ok = true;
    }

    // 将当前行追加到响应缓冲区
    size_t current_len = strlen(g_at_response_buffer);
    if (current_len < AT_RESPONSE_BUF_SIZE - 1) {
        strncat(g_at_response_buffer, line, AT_RESPONSE_BUF_SIZE - current_len - 1);
        current_len = strlen(g_at_response_buffer);
        // 如果不是最后一行，且还有空间，则添加换行符
        if (!is_final_response && current_len < AT_RESPONSE_BUF_SIZE - 2) {
            strcat(g_at_response_buffer, "\n");
        }
    } else {
        HAL_LOGW("Response buffer overflow, content truncated.");
    }

    // 如果是最终响应，释放信号量以唤醒等待的 `uwb_at_send_cmd_sync`
    if (is_final_response && g_at_sem_initialized) {
        HAL_LOGD("Final response received, posting semaphore.");
        sem_post(&g_at_response_sem);
    }
}
