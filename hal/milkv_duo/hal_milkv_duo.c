/**
 * @file hal_milkv_duo.c
 * @brief MilkV-Duo (Linux) 平台的硬件抽象层 (HAL) 实现
 * @details 实现了 uwb_hal.h 中定义的接口，适配 MilkV-Duo 的 Linux 环境，
 *          使用 wiringX 库进行串口操作，并使用 POSIX 标准 API 进行延时和线程同步。
 * @date 2025-06-07
 */

#include "../include/uwb_hal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <semaphore.h>
#include <sys/time.h>
#include <time.h>

// Correctly include the one and only wiringX header
#include "wiringx.h"

/* ------------------------- 内部变量 ------------------------- */
static int g_uart_fd = -1; // 全局串口文件描述符

/* ------------------------- HAL UART 接口实现 ------------------------- */

/**
 * @brief 初始化串口
 * @param[in] config 指向串口配置参数的指针
 * @return 0 表示成功，-1 表示失败
 */
int hal_uart_init(const uwb_hal_uart_config_t* config)
{
    if (config == NULL || config->device_name == NULL) {
        HAL_LOGE("Invalid config for UART init.");
        return -1;
    }

    // 初始化 wiringX 库
    if (wiringXSetup("duo", NULL) == -1) {
        HAL_LOGE("wiringX setup failed!");
        return -1;
    }

    // 准备 wiringX 的串口配置
    struct wiringXSerial_t serial_config;
    serial_config.baud = config->baud_rate;
    serial_config.databits = 8;
    serial_config.parity = 0;     // 0 = None
    serial_config.stopbits = 1;
    serial_config.flowcontrol = 0; // 0 = None

    // 使用正确的 wiringX 函数打开串口
    g_uart_fd = wiringXSerialOpen(config->device_name, serial_config);
    
    if (g_uart_fd < 0) {
        HAL_LOGE("Failed to open serial port %s with wiringX, error: %s", config->device_name, strerror(errno));
        return -1;
    }

    HAL_LOGI("Serial port %s opened with baud rate %u, FD: %d", 
             config->device_name, config->baud_rate, g_uart_fd);
             
    // 清空串口缓冲区
    wiringXSerialFlush(g_uart_fd);

    return 0;
}

/**
 * @brief 关闭串口
 */
void hal_uart_deinit(void)
{
    if (g_uart_fd != -1) {
        wiringXSerialClose(g_uart_fd);
        g_uart_fd = -1;
        HAL_LOGI("Serial port closed.");
    }
}

/**
 * @brief 通过串口写入指定长度的原始数据
 * @param[in] data 指向要发送数据的指针
 * @param[in] len 要发送的数据长度
 * @return 成功发送的字节数；-1 表示错误
 */
int hal_uart_write(const uint8_t* data, size_t len)
{
    if (g_uart_fd == -1 || data == NULL || len == 0) {
        HAL_LOGE("Invalid parameters or UART not initialized for write.");
        return -1;
    }
    
    hal_delay_ms(20);
    int bytes_written = write(g_uart_fd, data, len);
    
    if (bytes_written < 0) {
        HAL_LOGE("Failed to write to serial port, error: %s", strerror(errno));
    }
    return bytes_written;
}

/**
 * @brief 从串口读取指定长度的原始数据
 * @param[out] buffer 用于存放接收数据的缓冲区
 * @param[in]  len    期望读取的数据长度
 * @param[in]  timeout_ms 超时时间 (毫秒)
 * @return 成功读取的字节数；0 表示超时；-1 表示错误
 */
int hal_uart_read(uint8_t* buffer, size_t len, uint32_t timeout_ms)
{
    if (g_uart_fd == -1 || buffer == NULL || len == 0) {
        HAL_LOGE("Invalid parameters or UART not initialized for read.");
        return -1;
    }

    fd_set read_fds;
    struct timeval tv;
    int retval;

    FD_ZERO(&read_fds);
    FD_SET(g_uart_fd, &read_fds);

    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    // 使用 select 监控文件描述符的可读状态，实现超时
    retval = select(g_uart_fd + 1, &read_fds, NULL, NULL, &tv);

    if (retval == -1) {
        HAL_LOGE("select() error: %s", strerror(errno));
        return -1;
    } else if (retval == 0) {
        return 0; // 超时
    } else {
        if (FD_ISSET(g_uart_fd, &read_fds)) {
            int bytes_read = read(g_uart_fd, buffer, len);
            if (bytes_read < 0) {
                HAL_LOGE("Failed to read from serial port, error: %s", strerror(errno));
                return -1;
            }
            return bytes_read;
        }
    }
    return -1; // 理论上不会执行到这里
}

/* ------------------------- HAL Delay 接口实现 ------------------------- */

/**
 * @brief 毫秒级延时/阻塞
 * @param[in] ms 要延时的毫秒数
 */
void hal_delay_ms(uint32_t ms)
{
    usleep(ms * 1000);
}

/* ------------------------- HAL Synchronization 接口实现 ------------------------- */

/**
 * @brief 创建一个同步对象 (通常是二进制信号量)
 * @return 成功则返回句柄，失败则返回 NULL
 */
hal_sync_handle_t hal_sync_create(void)
{
    sem_t* sem = (sem_t*)malloc(sizeof(sem_t));
    if (sem == NULL) {
        HAL_LOGE("Failed to allocate memory for semaphore.");
        return NULL;
    }
    
    // 初始化信号量，pshared=0 表示线程间共享，value=0 表示初始时不可用
    if (sem_init(sem, 0, 0) == -1) {
        HAL_LOGE("Failed to initialize semaphore, error: %s", strerror(errno));
        free(sem);
        return NULL;
    }
    
    HAL_LOGD("Semaphore created: %p", (void*)sem);
    return (hal_sync_handle_t)sem;
}

/**
 * @brief 销毁一个同步对象
 * @param[in] handle 要销毁的句柄
 */
void hal_sync_destroy(hal_sync_handle_t handle)
{
    if (handle != NULL) {
        sem_t* sem = (sem_t*)handle;
        if (sem_destroy(sem) == -1) {
            HAL_LOGE("Failed to destroy semaphore, error: %s", strerror(errno));
        }
        free(sem);
        HAL_LOGD("Semaphore destroyed: %p", (void*)handle);
    }
}

/**
 * @brief 等待信号 (获取信号量)
 * @details 阻塞当前线程，直到接收到信号或超时。
 * @param[in] handle 同步对象句柄
 * @param[in] timeout_ms 超时时间 (毫秒)。如果为 0，则无限等待。
 * @return 0 表示成功接收到信号，-1 表示超时或错误
 */
int hal_sync_wait(hal_sync_handle_t handle, uint32_t timeout_ms)
{
    if (handle == NULL) {
        HAL_LOGE("Invalid semaphore handle for wait.");
        return -1;
    }
    
    sem_t* sem = (sem_t*)handle;
    int retval;

    if (timeout_ms == 0) { // 无限等待
        retval = sem_wait(sem);
    } else {
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
        
        retval = sem_timedwait(sem, &ts);
    }

    if (retval == -1) {
        if (errno == ETIMEDOUT) {
            HAL_LOGD("Semaphore wait timed out.");
        } else {
            HAL_LOGE("Semaphore wait error: %s", strerror(errno));
        }
        return -1; // 超时或错误均返回-1
    }
    
    return 0; // 成功
}

/**
 * @brief 发送信号 (释放信号量)
 * @param[in] handle 同步对象句柄
 */
void hal_sync_post(hal_sync_handle_t handle)
{
    if (handle == NULL) {
        HAL_LOGE("Invalid semaphore handle for post.");
        return;
    }
    
    sem_t* sem = (sem_t*)handle;
    if (sem_post(sem) == -1) {
        HAL_LOGE("Failed to post semaphore, error: %s", strerror(errno));
    }
}
