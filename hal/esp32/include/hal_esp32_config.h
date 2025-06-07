#ifndef HAL_ESP32_CONFIG_H
#define HAL_ESP32_CONFIG_H

#include "driver/uart.h"
#include "driver/gpio.h"

/* ------------------------- 模式与配置宏 ------------------------- */

/**
 * @brief 硬件在环 (HIL) 仿真模式开关
 * @details 定义为 1 时，应用将通过一个独立的 UART 与运行在同一芯片上的 UWB 模块仿真器通信。
 *          注释掉此宏以在真实硬件上运行。
 */
#define UWB_DUAL_UART_SIM_MODE 1

/**
 * @brief UWB 模块数据缓冲区大小 (字节)
 */
#define UWB_BUFFER_SIZE 1024

/* ------------------------- ESP32 平台硬件引脚定义 ------------------------- */

#ifdef UWB_DUAL_UART_SIM_MODE

    /* ----- 仿真模式下的引脚和 UART 配置 ----- */
    // 应用层使用的 UART (与仿真器通信)
    #define UWB_UART_NUM      (UART_NUM_2)
    #define UWB_UART_TX_PIN   (GPIO_NUM_17)
    #define UWB_UART_RX_PIN   (GPIO_NUM_16)

    // 仿真器使用的 UART (模拟 UWB 模块)
    #define SIM_UART_NUM      (UART_NUM_1)
    #define UWB_SIM_UART_TX_PIN (GPIO_NUM_19)
    #define UWB_SIM_UART_RX_PIN (GPIO_NUM_18)

#else

    /* ----- 真实硬件模式下的引脚和 UART 配置 ----- */
    // 连接真实 UWB 模块的 UART
    #define UWB_UART_NUM      (UART_NUM_2)
    #define UWB_UART_TX_PIN   (GPIO_NUM_17)
    #define UWB_UART_RX_PIN   (GPIO_NUM_18)
    // 根据实际硬件连接定义其他引脚 (RST, WAKEUP 等)
    // #define UWB_RST_PIN    (GPIO_NUM_19)
    // #define UWB_WAKEUP_PIN (GPIO_NUM_21)

#endif

#endif // HAL_ESP32_CONFIG_H 