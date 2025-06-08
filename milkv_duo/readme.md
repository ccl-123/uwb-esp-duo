# UWB MK8000 驱动 - MilkV-Duo 示例应用

## 1. 项目概述

此目录包含 **UWB MK8000 驱动程序**在 **MilkV-Duo (256M)** 嵌入式 Linux 平台上的示例应用程序。
## 2. 核心功能

`main.c` 中的程序执行以下操作：

1.  **初始化硬件抽象层 (HAL)**：调用 `hal_uart_init()` 初始化 MilkV-Duo 用于和 UWB 模块通信的 UART 端口 (默认为 `UART3`)。
2.  **初始化 UWB 驱动**：调用 `uwb_driver_init()`，将数据处理的回调函数 `ranging_data_handler` 注册到驱动核心。
3.  **配置 UWB 模块**：
    - 定义一个 `uwb_settings_t` 结构体，设置模块为**主机 (Master)** 模式，并配置好网络ID、从机地址等关键参数。
    - 调用 `uwb_configure_module()` 应用这些设置。此函数内部包含了**自动重试机制**，如果模块配置失败，它会最多重试 `UWB_ENTER_AT_RETRIES` 次。
4.  **进入主循环**：
    - 程序进入一个无限循环 `while(1)`。
    - 在循环中，通过 `hal_uart_read()` 不断地从串口读取数据。
    - 将读取到的数据喂给 `uwb_driver_process_data()` 进行解析。
    - `uwb_driver_process_data()` 在解析出完整的测距数据帧后，会自动调用之前注册的 `ranging_data_handler` 回调函数。
6.  **打印测距数据**：`ranging_data_handler` 函数负责将接收到的测距信息（如从机地址、距离、信号强度）格式化并打印到控制台。

## 3. 硬件连接

请根据以下引脚定义将 UWB MK8000 模块连接到 MilkV-Duo 开发板。

| UWB 模块引脚 | MilkV-Duo 引脚 | 说明 |
| :--- | :--- | :--- |
| VCC (3.3V) | 3.3V | 电源正极 |
| GND | GND | 电源地 |
| **TX** (GPIO1) | **GP2** (Pin 28) | MilkV-Duo 的 **UART3_RX** |
| **RX** (GPIO0) | **GP1** (Pin 29) | MilkV-Duo 的 **UART3_TX** |

> **注意**：交叉连接是必须的，即模块的 TX 连接到主控的 RX，模块的 RX 连接到主控的 TX。

## 4. 编译与运行

本项目使用 `CMake` 进行构建，需要使用 MilkV-Duo 的交叉编译工具链。
1.  **加载编译环境**:
    ```bash
    source /home/cl/MilkV_Duo_project/duo-examples/envsetup.sh
    ```
    在弹出的选项中，根据提示选择 `milkv-duo` 平台。

2.  **进入编译目录**:
    ```bash
    cd /path/to/uwb_mk8000_refactor/milkv_duo
    ```

3.  **执行编译**:
    ```bash
    make clean && make
    ```
    编译成功后，可执行文件 `uwb_demo` 将在当前目录 (`milkv_duo/`) 生成。
# 6. 将可执行文件传输到 MilkV-Duo 开发板
#    例如，使用 scp:
#    scp milkv_duo/uwb_duo_example root@<your_duo_ip>:/root/

# 7. 在 MilkV-Duo 的终端上，给文件添加执行权限并运行
chmod +x /root/uwb_duo_example
/root/uwb_duo_example
```


