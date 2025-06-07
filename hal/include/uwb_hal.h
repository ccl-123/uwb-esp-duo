/**
 * @file uwb_hal.h
 * @brief UWB 驱动硬件抽象层 (HAL) 接口定义
 * @details 定义了 UWB 核心逻辑与底层硬件平台交互的标准接口。
 *          通过这些抽象接口，UWB 驱动的核心逻辑可以实现跨平台移植。
 * @date 2025-06-07
 */

#ifndef UWB_HAL_H
#define UWB_HAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------- 宏定义 ------------------------- */
/**
 * @brief 用于跨平台日志输出的宏
 * @details 默认映射到标准 C 库的 printf 函数。
 *          各平台实现可以重定向 printf 的输出到相应的调试控制台。
 */
#define HAL_LOGI(format, ...) printf("[UWB_INFO] " format "\n", ##__VA_ARGS__)
#define HAL_LOGW(format, ...) printf("[UWB_WARN] " format "\n", ##__VA_ARGS__)
#define HAL_LOGE(format, ...) printf("[UWB_ERROR] " format "\n", ##__VA_ARGS__)
#define HAL_LOGD(format, ...) printf("[UWB_DEBUG] " format "\n", ##__VA_ARGS__)

/* ------------------------- 结构体定义 ------------------------- */

/**
 * @brief UWB HAL UART 配置结构体
 */
typedef struct {
    const char* device_name; ///< 串口设备名 (例如 "1" 代表 ESP32 的 UART1, "/dev/ttyS1" 代表 Linux 设备)
    uint32_t baud_rate;      ///< 波特率
    // 以下参数主要用于类-RTOS系统，Linux系统可忽略
    uint32_t rx_buffer_size; ///< 接收缓冲区大小
    uint32_t tx_buffer_size; ///< 发送缓冲区大小
    int tx_pin;              ///< TX 引脚 (仅适用于需要引脚配置的平台，如 ESP32)
    int rx_pin;              ///< RX 引脚 (仅适用于需要引脚配置的平台，如 ESP32)
} uwb_hal_uart_config_t;

/* ------------------------- HAL 接口函数声明 ------------------------- */

/**
 * @brief 初始化串口
 * @param[in] config 指向串口配置参数的指针
 * @return 0 表示成功，-1 表示失败
 */
int hal_uart_init(const uwb_hal_uart_config_t* config);

/**
 * @brief 关闭串口
 */
void hal_uart_deinit(void);

/**
 * @brief 通过串口写入指定长度的原始数据
 * @param[in] data 指向要发送数据的指针
 * @param[in] len 要发送的数据长度
 * @return 成功发送的字节数；-1 表示错误
 */
int hal_uart_write(const uint8_t* data, size_t len);

/**
 * @brief 从串口读取指定长度的原始数据
 * @param[out] buffer 用于存放接收数据的缓冲区
 * @param[in]  len    期望读取的数据长度
 * @param[in]  timeout_ms 超时时间 (毫秒)
 * @return 成功读取的字节数；0 表示超时；-1 表示错误
 */
int hal_uart_read(uint8_t* buffer, size_t len, uint32_t timeout_ms);

/**
 * @brief 毫秒级延时/阻塞
 * @param[in] ms 要延时的毫秒数
 */
void hal_delay_ms(uint32_t ms);


/**
 * @brief 线程同步句柄
 * @details 定义一个通用的、不透明的同步句柄类型，
 *          底层可由信号量(Semaphore)或互斥锁(Mutex)等实现。
 */
typedef void* hal_sync_handle_t;

/**
 * @brief 创建一个同步对象 (通常是二进制信号量)
 * @return 成功则返回句柄，失败则返回 NULL
 */
hal_sync_handle_t hal_sync_create(void);

/**
 * @brief 销毁一个同步对象
 * @param[in] handle 要销毁的句柄
 */
void hal_sync_destroy(hal_sync_handle_t handle);

/**
 * @brief 等待信号 (获取信号量)
 * @details 阻塞当前线程，直到接收到信号或超时。
 * @param[in] handle 同步对象句柄
 * @param[in] timeout_ms 超时时间 (毫秒)。如果为 0，则无限等待。
 * @return 0 表示成功接收到信号，-1 表示超时或错误
 */
int hal_sync_wait(hal_sync_handle_t handle, uint32_t timeout_ms);

/**
 * @brief 发送信号 (释放信号量)
 * @param[in] handle 同步对象句柄
 */
void hal_sync_post(hal_sync_handle_t handle);


#ifdef __cplusplus
}
#endif

#endif // UWB_HAL_H
