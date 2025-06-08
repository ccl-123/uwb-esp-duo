/**
 * @file uwb_mk8000.h
 * @brief MK8000PATR7.9-GC UWB 模块驱动程序公共 API 接口定义 (重构)
 * @details 定义了与 UWB 模块交互所需的数据结构、枚举、回调函数以及
 *          上层应用可以调用的主要 API 函数。此头文件应为平台无关的。
 * @date 2025-06-08
 */

#ifndef UWB_MK8000_H
#define UWB_MK8000_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "uwb_hal.h" // 引入硬件抽象层

/* ------------------------- 模式与配置宏 ------------------------- */

/**
 * @brief UWB 模块数据缓冲区大小 (字节)
 * @details 此宏可被平台相关的配置文件覆盖。
 */
#ifndef UWB_BUFFER_SIZE
#define UWB_BUFFER_SIZE 1024
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------- 宏定义 ------------------------- */
#define UWB_DEFAULT_BAUD_RATE 115200 ///< 模块出厂默认波特率
#define UWB_DEFAULT_NETWORK_ID 255   ///< 模块出厂默认网络ID
#define UWB_DEFAULT_PERIOD 100       ///< 模块出厂默认测距周期 (100 * 10ms = 1s)
#define AT_CMD_TIMEOUT_MS (2000)     ///< AT 指令响应默认超时时间
#define UWB_ENTER_AT_RETRIES 3       ///< 进入AT指令模式的重试次数

/* ------------------------- 缓冲区大小定义 ------------------------- */
#define UWB_UART_RX_BUFFER_SIZE     (1024 * 2)  ///< UART 接收缓冲区大小
#define UWB_UART_TX_BUFFER_SIZE     512         ///< UART 发送缓冲区大小

/* ------------------------- UWB设备地址定义 ------------------------- */
#define UWB_MASTER_SELF_ADDR        0x0000  ///< 主机（基站）本机地址
#define UWB_SLAVE_ADDR_0            0x0001  ///< 从机0地址
#define UWB_SLAVE_ADDR_1            0x0002  ///< 从机1地址
#define UWB_SLAVE_ADDR_2            0x0003  ///< 从机2地址

/* ------------------------- 测距参数定义 ------------------------- */
#define UWB_RANGING_PERIOD  20      ///< 测距周期因子（实际周期 = 值 * 10ms = 200ms）


/* ------------------------- 枚举定义 ------------------------- */

/**
 * @brief UWB 模块角色定义
 */
typedef enum {
    UWB_ROLE_SLAVE = 0, ///< 从机模式 (标签)
    UWB_ROLE_MASTER = 1 ///< 主机模式 (基站)
} uwb_role_t;

/**
 * @brief UWB 模块工作模式定义
 */
typedef enum {
    UWB_MODE_AT_COMMAND = 0, ///< AT 指令模式
    UWB_MODE_RANGING = 1     ///< 测距模式 (默认)
} uwb_mode_t;

/**
 * @brief UWB 模块低功耗模式定义
 */
typedef enum {
    UWB_LPWR_OFF = 0, ///< 关闭低功耗模式 (默认)
    UWB_LPWR_ON = 1   ///< 开启低功耗模式
} uwb_lpwr_t;

/* ------------------------- 结构体定义 ------------------------- */

/**
 * @brief UWB 模块设置结构体 (用于配置)
 */
typedef struct {
    uwb_role_t role;          ///< 模块角色
    uint16_t self_address;    ///< 本机地址 (HEX)
    uint16_t master_address;  ///< 主机地址 (HEX) (仅从机模式有效)
    uint16_t slave_addr_0;    ///< 从机0地址 (HEX) (仅主机模式有效)
    uint16_t slave_addr_1;    ///< 从机1地址 (HEX) (仅主机模式有效)
    uint16_t slave_addr_2;    ///< 从机2地址 (HEX) (仅主机模式有效)
    uint8_t  network_id;      ///< 网络ID (0-255)
    uint8_t  ranging_period;  ///< 测距周期 (5-100, 单位 10ms)
    uwb_lpwr_t low_power_mode;///< 低功耗模式
} uwb_settings_t;

/**
 * @brief UWB 测距数据结构体
 */
typedef struct {
    uint16_t sender_address; ///< 发送方地址
    uint16_t distance_cm;    ///< 距离 (cm)
    int8_t   rssi_dbm;       ///< 信号强度 (dBm)
} uwb_ranging_data_t;

/* ------------------------- 回调函数定义 ------------------------- */

/**
 * @brief 测距数据回调函数指针类型
 * @param[in] data 指向接收到的测距数据结构体的指针
 */
typedef void (*uwb_ranging_callback_t)(const uwb_ranging_data_t* data);

/**
 * @brief AT 指令响应回调函数指针类型 (可选，用于异步处理)
 * @param[in] response 指向接收到的 AT 响应字符串的指针
 */
typedef void (*uwb_at_response_callback_t)(const char* response);


/* ------------------------- 公共API函数声明 ------------------------- */

/**
 * @brief 初始化 UWB 驱动程序
 * @details 初始化驱动核心逻辑，并设置回调函数。
 *          注意：此函数不再负责初始化 UART，UART 的初始化需通过调用 hal_uart_init 完成。
 * @param[in] ranging_cb  接收到测距数据时的回调函数。
 * @return int 0 表示成功, -1 表示失败 (例如内存分配失败)
 */
int uwb_driver_init(uwb_ranging_callback_t ranging_cb);

/**
 * @brief 卸载 UWB 驱动程序
 * @details 释放驱动核心逻辑占用的资源。
 * @return int 0 表示成功
 */
int uwb_driver_deinit(void);

/**
 * @brief 处理从硬件接收到的原始数据流
 * @details 上层应用在通过 hal_uart_read() 读取到数据后，应调用此函数将数据喂给驱动核心进行解析。
 *          驱动核心会缓冲数据，识别完整的 AT 响应或测距帧，并触发相应的回调。
 * @param[in] data 接收到的数据指针
 * @param[in] len  接收到的数据长度
 */
void uwb_driver_process_data(const uint8_t* data, size_t len);

/**
 * @brief 设置 UWB 模块的工作模式
 * @details 发送 AT+MODE 指令切换模块工作模式。
 * @param[in] mode 要设置的工作模式 (AT 指令或测距)。
 * @return int 0 表示成功, -1 表示失败或超时
 */
int uwb_set_work_mode(uwb_mode_t mode);

/**
 * @brief 配置 UWB 模块参数
 * @details 自动进入 AT 模式，发送一系列配置指令，然后复位模块使配置生效。
 * @param[in] settings 包含所有待配置参数的结构体。
 * @return int 0 表示成功, -1 表示某个配置步骤失败或超时
 */
int uwb_configure_module(const uwb_settings_t* settings);

/**
 * @brief 软件复位 UWB 模块
 * @details 发送 AT+RST 指令。
 * @return int 0 表示成功, -1 表示失败或超时
 */
int uwb_software_reset(void);

/**
 * @brief 恢复 UWB 模块出厂设置
 * @details 发送 AT+DEFT 指令。
 * @return int 0 表示成功, -1 表示失败或超时
 */
int uwb_factory_reset(void);

/**
 * @brief 查询 UWB 模块固件版本
 * @details 发送 AT+VER 指令。
 * @param[out] buffer 存储查询结果的缓冲区。
 * @param[in]  buffer_len 缓冲区大小。
 * @return int 0 表示成功, -1 表示失败或超时
 */
int uwb_query_version(char* buffer, size_t buffer_len);

/**
 * @brief 查询 UWB 模块所有参数
 * @details 发送 AT+ALL 指令。
 * @param[out] buffer 存储查询结果的缓冲区。
 * @param[in]  buffer_len 缓冲区大小。
 * @return int 0 表示成功, -1 表示失败或超时
 */
int uwb_query_all_params(char* buffer, size_t buffer_len);


#ifdef __cplusplus
}
#endif

#endif // UWB_MK8000_H
