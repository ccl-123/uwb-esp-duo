/**
 * @file main.c
 * @brief UWB 驱动在 MilkV-Duo (Linux) 平台上的应用示例
 * @details 初始化 HAL 和 UWB 驱动，配置模块，以及如何创建一个
 *          独立的线程来接收和处理 UWB 数据。
 * @date 2025-06-07
 */

#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

#include "uwb_mk8000.h" // 重构后的驱动头文件

#define UART_DEVICE "/dev/ttyS1" // MilkV-Duo 上的 UART 设备，根据实际情况修改
#define RX_BUFFER_SIZE 512       // 接收线程的缓冲区大小

static bool g_is_running = true; // 用于控制接收线程的运行

/**
 * @brief 测距数据回调处理函数
 * @details 当 UWB 驱动解析到一帧测距数据时，此函数会被调用。
 * @param[in] data 指向测距数据的指针。
 */
void ranging_data_handler(const uwb_ranging_data_t* data)
{
    if (data) {
        printf("Ranging Data: Addr=0x%04X, Dist=%u cm, RSSI=%d dBm\n",
               data->sender_address,
               data->distance_cm,
               data->rssi_dbm);
    }
}

/**
 * @brief UWB 数据接收线程函数
 * @details 该线程在一个循环中持续从串口读取数据，并将其喂给 UWB 驱动核心进行处理。
 * @param[in] arg 未使用
 * @return void*
 */
void* uwb_receive_thread(void* arg)
{
    (void)arg; // 避免编译器警告
    uint8_t rx_buffer[RX_BUFFER_SIZE];

    printf("UWB receive thread started.\n");

    while (g_is_running) {
        // 尝试从串口读取数据，超时时间设置为 100ms
        int len = hal_uart_read(rx_buffer, RX_BUFFER_SIZE, 100);
        if (len > 0) {
            // 如果读到数据，就喂给驱动核心
            uwb_driver_process_data(rx_buffer, len);
        }
        // 如果 len == 0 (超时) 或 len < 0 (错误)，则继续下一次循环
    }

    printf("UWB receive thread finished.\n");
    return NULL;
}

/**
 * @brief 主应用程序入口
 */
int main(void)
{
    pthread_t rx_thread_id;
    int ret;

    printf("--- UWB MK8000 Driver Example for MilkV-Duo ---\n");

    // 1. 定义 HAL 层的 UART 配置
    uwb_hal_uart_config_t uart_config = {
        .device_name = UART_DEVICE,
        .baud_rate = UWB_DEFAULT_BAUD_RATE,
        // 以下参数在 Linux HAL 中未使用，但为保持结构一致性而保留
        .rx_buffer_size = UWB_BUFFER_SIZE * 2,
        .tx_buffer_size = UWB_BUFFER_SIZE,
        .tx_pin = -1,
        .rx_pin = -1,
    };

    // 2. 初始化 HAL 层的 UART
    if (hal_uart_init(&uart_config) != 0) {
        fprintf(stderr, "Failed to initialize HAL UART.\n");
        return -1;
    }

    // 3. 初始化 UWB 驱动核心逻辑
    if (uwb_driver_init(ranging_data_handler) != 0) {
        fprintf(stderr, "Failed to initialize UWB driver.\n");
        hal_uart_deinit();
        return -1;
    }

    // 4. 创建并启动数据接收线程
    ret = pthread_create(&rx_thread_id, NULL, uwb_receive_thread, NULL);
    if (ret != 0) {
        fprintf(stderr, "Failed to create receive thread. Error: %s\n", strerror(ret));
        uwb_driver_deinit();
        hal_uart_deinit();
        return -1;
    }

    // 延时一小段时间，确保接收线程已准备好
    sleep(1);

    // 5. 定义模块配置 (配置为"主机")
    uwb_settings_t my_settings = {
        .role = UWB_ROLE_MASTER,
        .self_address = UWB_MASTER_SELF_ADDR,
        .master_address = UWB_MASTER_SELF_ADDR, // 从机模式下使用
        .slave_addr_0 = UWB_SLAVE_ADDR_0,
        .slave_addr_1 = UWB_SLAVE_ADDR_1,
        .slave_addr_2 = UWB_SLAVE_ADDR_2,
        .network_id = UWB_DEFAULT_NETWORK_ID,
        .ranging_period = UWB_RANGING_PERIOD,
        .low_power_mode = UWB_LPWR_OFF
    };

    // 6. 配置 UWB 模块
    printf("Configuring UWB module...\n");
    if (uwb_configure_module(&my_settings) != 0) {
        fprintf(stderr, "Failed to configure UWB module!\n");
    } else {
        printf("UWB module configured successfully. Waiting for ranging data...\n");
    }

    // 7. 主线程可以执行其他任务，或只是等待
    //    这里我们简单地等待用户输入来停止程序
    printf("Press Enter to exit.\n");
    getchar();

    // 8. 停止接收线程并清理资源
    g_is_running = false;
    pthread_join(rx_thread_id, NULL); // 等待接收线程退出

    uwb_driver_deinit();
    hal_uart_deinit();

    printf("Application finished.\n");

    return 0;
}
