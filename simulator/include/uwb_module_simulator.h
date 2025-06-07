#ifndef UWB_MODULE_SIMULATOR_H
#define UWB_MODULE_SIMULATOR_H

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#include "uwb_mk8000.h"

// 定义了仿真器所需的引脚等宏
#include "hal_esp32_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief UWB模块模拟器UART配置结构体
 */
typedef struct {
    uart_port_t uart_num;
    int tx_pin;
    int rx_pin;
    int baud_rate;
    int rx_buffer_size;
    int tx_buffer_size;
} uwb_simulator_uart_config_t;

/**
 * @brief 初始化UWB模块模拟器
 * @param[in] sim_uart_config 模拟器UART配置参数。
 * @return 0 表示成功, -1 表示失败
 */
int uwb_simulator_init(const uwb_simulator_uart_config_t* sim_uart_config);

/**
 * @brief 卸载UWB模块模拟器
 * @return 0 表示成功
 */
int uwb_simulator_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // UWB_MODULE_SIMULATOR_H 