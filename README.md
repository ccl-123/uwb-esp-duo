# UWB MK8000 跨平台驱动（MilkV-Duo & ESP32）

## 1. 项目简介

本项目旨在将一款 UWB (超宽带) MK8000 模块的驱动代码进行重构，实现一个清晰、可移植、易于维护的分层式驱动架构。

重构后的驱动同时支持 **MilkV-Duo (RISC-V, Linux)** 和 **ESP32 (Xtensa, FreeRTOS)** 两个异构平台，展示了如何通过硬件抽象层 (HAL) 设计，使同一套核心业务逻辑代码运行在完全不同的软硬件环境上。

### 主要特性

- **跨平台支持**: 一套核心代码，无缝运行在 MilkV-Duo 和 ESP32 平台上。
- **分层架构**: 采用 `核心逻辑层 -> HAL接口 -> HAL实现` 的分层设计。
- **平台隔离**: 每个平台的硬件配置和实现都相互独立，互不干扰。
- **仿真模式 (ESP32)**: 内置硬件在环 (HIL) 仿真模式，在没有物理 UWB 模块的情况下也能开发和调试上层应用。

## 2. 架构与目录结构

### 2.1. 分层架构

```
+------------------------------------------+
|         应用层 (main.c)                  |
|  (业务逻辑，例如配置模块、处理数据)        |
+------------------------------------------+
|      核心驱动层 (uwb_core, uwb_at)       |
+|  (平台无关的协议解析、AT指令、状态机)      |
++------------------------------------------+
+|       |             ^                    |
+|       v             | (POSIX API: sem_wait, etc.)
+| HAL 接口 (UART)  |                    |
+| (uwb_hal.h)       |                    |
 +---------------------+--------------------+
 |                HAL 实现层                 |
 | +------------------+  +-----------------+ |
| |   ESP32 实现     |  | MilkV-Duo 实现  | |
| | (hal_esp32.c)    |  | (hal_milkv_duo.c) | |
| +------------------+  +-----------------+ |
+------------------------------------------+
```

**架构亮点**:

- **混合抽象模型**:
  - **硬件相关操作 (UART)**: 通过自定义的 HAL 接口 (`hal_uart_*`) 进行抽象，以适配不同平台的物理硬件差异。
  - **OS 标准服务 (线程同步)**: 核心驱动层直接使用 `semaphore.h` 提供的 **POSIX API**。这利用了 ESP-IDF 和 Linux 两个平台对该标准的共同支持，减少了不必要的封装，使代码更标准化。

### 2.2. 目录结构

```
uwb_mk8000_refactor/
├── build/                     # 编译输出目录 (主要由 ESP-IDF 使用)
├── hal/                       # 硬件抽象层 (HAL)
│   ├── include/               # HAL 统一接口头文件 (uwb_hal.h)
│   ├── esp32/
│   │   ├── include/           # ESP32 平台专属配置头文件
│   │   │   └── hal_esp32_config.h
│   │   └── hal_esp32.c        # ESP32 HAL 实现
│   └── milkv_duo/
│       └── hal_milkv_duo.c    # MilkV-Duo HAL 实现
├── include/                   # 核心驱动的公共头文件 (uwb_mk8000.h)
├── main/                      # ESP32 平台主程序入口
│   ├── CMakeLists.txt         # ESP32 主组件构建配置
│   └── main.c                 # ESP32 主程序入口文件
├── milkv_duo/                 # MilkV-Duo 平台的示例应用和编译入口
│   ├── main.c
│   └── Makefile
├── simulator/                 # 硬件在环(HIL)仿真模块 (ESP32 专用)
│   ├── include/
│   │   └── uwb_module_simulator.h
│   └── uwb_module_simulator.c
├── src/                       # 平台无关的核心驱动源代码
│   ├── uwb_at.c
│   └── uwb_core.c
├── .gitignore
├── CMakeLists.txt             # ESP-IDF 项目根 CMake 文件
└── README.md                  # 本项目说明文档
```

---

## 3. MilkV-Duo 平台使用指南

### 3.1. 环境准备

-   **交叉编译工具链**: 确保已正确安装 MilkV-Duo 官方 SDK，并能通过 `source` 命令加载其环境配置脚本 (如 `envsetup.sh`)。
-   **硬件依赖**: 目标板子上需要存在 `wiringX` 库，用于串口操作。

### 3.2. 编译步骤

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

### 3.3. 运行与部署

1.  **上传文件**: 使用 `scp` 将编译好的 `uwb_demo` 上传到 MilkV-Duo 开发板。
    ```bash
    # 假设 MilkV-Duo 的 IP 地址是 192.168.42.1
    scp uwb_demo root@192.168.42.1:/root/
    ```

2.  **运行程序**: SSH 登录到开发板后，添加执行权限并运行。
    ```bash
    chmod +x /root/uwb_demo
    /root/uwb_demo
    ```

---

## 4. ESP32 平台使用指南

### 4.1. 环境准备

-   **ESP-IDF**: 请确保已根据乐鑫官方文档正确安装了 ESP-IDF 开发环境。本项目基于 ESP-IDF 的 `CMake` 和 `idf.py` 工具进行构建。

### 4.2. 平台配置

-   所有特定于 ESP32 的硬件配置（如 UART 端口号、引脚定义、仿真模式开关）都集中在 `hal/esp32/include/hal_esp32_config.h` 文件中。
-   可以直接修改此文件来适配开发板硬件。

### 4.3. 编译步骤

1.  **进入项目根目录**:
    ```bash
    cd /path/to/uwb_mk8000_refactor
    ```

2.  **加载 ESP-IDF 环境**:
    ```bash
    # 请替换为实际 ESP-IDF 路径
    . $IDF_PATH/export.sh
    ```

3.  **设置目标芯片** (以 ESP32-S3 为例):
    ```bash
    idf.py set-target esp32s3
    ```

4.  **执行编译**:
    ```bash
    idf.py build
    ```
    编译成功后，固件及可执行文件将位于 `build/` 目录下。

### 4.4. 烧录与监视

-   **连接开发板**: 将您的 ESP32 开发板通过 USB 连接到电脑。
-   **一键烧录和监视**:
    ```bash
    idf.py flash monitor
    ```
-   **退出监视器**: 按 `Ctrl + ]` 组合键可以退出串口监视器。

### 4.5. 仿真模式

-   通过修改 `hal/esp32/include/hal_esp32_config.h` 中的 `#define UWB_DUAL_UART_SIM_MODE 1` 宏可以开启或关闭仿真模式。
-   开启后，应用主逻辑将与运行在同一芯片上的仿真器通过 UART 进行通信，无需连接物理 UWB 模块。
