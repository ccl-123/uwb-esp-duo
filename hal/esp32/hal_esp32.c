/**
 * @file hal_esp32.c
 * @brief ESP32 平台的硬件抽象层 (HAL) 实现
 * @details 实现了 uwb_hal.h 中定义的接口，适配 ESP-IDF 环境。
 * @date 2025-06-08
 */

#include "uwb_hal.h"
#include "hal_esp32_config.h" 
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "HAL_ESP32";

// 全局变量来存储初始化时确定的UART端口号
static uart_port_t g_hal_uart_port = UART_NUM_MAX;

int hal_uart_init(const uwb_hal_uart_config_t* config) {
    if (config == NULL || config->device_name == NULL) {
        HAL_LOGE("Invalid config for UART init.");
        return -1;
    }

    g_hal_uart_port = (uart_port_t)atoi(config->device_name);
    if (g_hal_uart_port >= UART_NUM_MAX) {
        HAL_LOGE("Invalid UART port number: %s", config->device_name);
        return -1;
    }

    uart_config_t uart_config = {
        .baud_rate = config->baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    esp_err_t err = uart_driver_install(g_hal_uart_port, config->rx_buffer_size, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        HAL_LOGE("Failed to install UART driver: %s", esp_err_to_name(err));
        return -1;
    }
    
    err = uart_param_config(g_hal_uart_port, &uart_config);
    if (err != ESP_OK) {
        HAL_LOGE("Failed to configure UART parameters: %s", esp_err_to_name(err));
        uart_driver_delete(g_hal_uart_port);
        return -1;
    }

    err = uart_set_pin(g_hal_uart_port, config->tx_pin, config->rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        HAL_LOGE("Failed to set UART pins: %s", esp_err_to_name(err));
        uart_driver_delete(g_hal_uart_port);
        return -1;
    }

    uart_flush_input(g_hal_uart_port);
    hal_delay_ms(20);
    uart_flush_input(g_hal_uart_port);
    HAL_LOGI("HAL UART%d initialized successfully.", g_hal_uart_port);
    return 0;
}

void hal_uart_deinit(void) {
    if (g_hal_uart_port != UART_NUM_MAX) {
        if (uart_is_driver_installed(g_hal_uart_port)) {
            uart_driver_delete(g_hal_uart_port);
            HAL_LOGI("HAL UART%d deinitialized.", g_hal_uart_port);
        }
        g_hal_uart_port = UART_NUM_MAX;
    }
}

int hal_uart_write(const uint8_t* data, size_t len) {
    if (g_hal_uart_port == UART_NUM_MAX) return -1;
    if (data == NULL || len == 0) return 0;
    hal_delay_ms(20);
    return uart_write_bytes(g_hal_uart_port, (const char *)data, len);
}

int hal_uart_read(uint8_t* buffer, size_t len, uint32_t timeout_ms) {
    if (g_hal_uart_port == UART_NUM_MAX) return -1;
    if (buffer == NULL || len == 0) return 0;

    return uart_read_bytes(g_hal_uart_port, buffer, len, pdMS_TO_TICKS(timeout_ms));
}

void hal_delay_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/*
hal_sync_handle_t hal_sync_create(void) {
    return (hal_sync_handle_t)xSemaphoreCreateBinary();
}

void hal_sync_destroy(hal_sync_handle_t handle) {
    if (handle != NULL) {
        vSemaphoreDelete((SemaphoreHandle_t)handle);
    }
}

int hal_sync_wait(hal_sync_handle_t handle, uint32_t timeout_ms) {
    if (handle == NULL) {
        return -1;
    }
    TickType_t ticks_to_wait = (timeout_ms == 0xFFFFFFFF) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    if (xSemaphoreTake((SemaphoreHandle_t)handle, ticks_to_wait) == pdTRUE) {
        return 0;
    } else {
        return -1;
    }
}

void hal_sync_post(hal_sync_handle_t handle) {
    if (handle != NULL) {
        xSemaphoreGive((SemaphoreHandle_t)handle);
    }
}
*/
