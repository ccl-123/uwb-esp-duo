# MilkV-Duo & ESP32 UWB MK8000 驱动重构项目

## 1. 项目简介

本项目旨在将一款 UWB (超宽带) MK8000 模块的驱动代码进行重构，实现一个分层的、可移植的、易于维护的驱动架构，并将其分别适配到 **MilkV-Duo (riscv64)** 和 **ESP32 (xtensa)** 两个不同的硬件平台上。

## 2. 目录结构

```
uwb_mk8000_refactor/
├── build/                # 编译输出目录 (用于 ESP-IDF)
├── hal/                  # 硬件抽象层 (Hardware Abstraction Layer)
│   ├── include/          # HAL 通用头文件 (uwb_hal.h)
│   ├── esp32/            # ESP32 平台特定的 HAL 实现 (hal_esp32.c)
│   └── milkv_duo/        # MilkV-Duo 平台特定的 HAL 实现 (hal_milkv_duo.c)
├── include/              # 核心驱动的公共头文件 (uwb_mk8000.h)
├── main/                 # ESP32 平台主程序入口
│   ├── CMakeLists.txt    # ESP32 主组件的构建配置
│   └── main.c            # ESP32 主程序入口文件
├── milkv_duo/            # MilkV-Duo 平台的示例应用层和编译入口
│   ├── main.c            # MilkV-Duo 主程序入口文件
│   └── Makefile          # MilkV-Duo 编译脚本
├── simulator/            # 硬件在环 (HIL) 仿真模块 (ESP32平台专用)
│   ├── include/          # 仿真模块头文件
│   └── uwb_module_simulator.c # 仿真模块源文件
├── src/                  # 核心驱动源代码 (平台无关)
│   ├── uwb_at.c          # AT 指令处理
│   └── uwb_core.c        # 驱动核心逻辑
├── .gitignore            # Git 版本控制忽略文件
├── CMakeLists.txt        # ESP-IDF 项目根 CMake 文件
├── README.md             # 本项目说明文档
└── refactor.md           #  重构过程记录
```

---

## 3. MilkV-Duo 平台编译指南

本项目依赖于 MilkV-Duo SDK 提供的 `envsetup.sh` 脚本来设置必要的环境变量（如 `TOOLCHAIN_PREFIX`, `CFLAGS`, `LDFLAGS`）。如果未加载此脚本，`Makefile` 将会报错并停止编译。

-   **工具链**: 本项目依赖 MilkV-Duo SDK 提供的 `envsetup.sh` 脚本来设置交叉编译工具链和环境变量。
-   **脚本路径**: 例如 `/home/cl/MilkV_Duo_project/duo-examples/envsetup.sh`。如果未加载此脚本，`Makefile` 将会报错。

### 3.2 编译步骤

1.  **加载编译环境**: 在终端中执行以下命令加载环境变量。
    ```bash
    source /home/cl/MilkV_Duo_project/duo-examples/envsetup.sh
    ```
    在弹出的选项中，输入 `2` 并回车，选择 `milkv-duo` 平台。

2.  **进入编译目录**:
    ```bash
    cd /path/to/uwb_mk8000_refactor/milkv_duo
    ```

3.  **执行编译**:
    ```bash
    make clean && make
    ```
    编译成功后，可执行文件 `uwb_mk8000` 将在当前目录生成。

### 3.3 运行与部署

1.  **上传文件**: 使用 `scp` 将编译好的 `uwb_mk8000` 上传到 MilkV-Duo 开发板。
    ```bash
    # MilkV-Duo 的 IP 地址是 192.168.42.1
    scp uwb_mk8000 root@192.168.42.1:/root/
    ```

2.  **运行程序**: SSH 登录到开发板后，添加执行权限并运行。
    ```bash
    chmod +x /root/uwb_mk8000
    /root/uwb_mk8000
    ```

---

## 4. ESP32 平台编译指南

### 4.1 环境准备

-   **ESP-IDF**: 请确保已根据乐鑫官方文档正确安装了 ESP-IDF 开发环境。本项目基于 ESP-IDF 的 `CMake` 和 `idf.py` 工具进行构建。

### 4.2 编译步骤

1.  **进入项目根目录**:
    ```bash
    cd /path/to/uwb_mk8000_refactor
    ```

2.  **加载 ESP-IDF 环境**:
    ```bash
    . $IDF_PATH/export.sh
    ```
    (请将 `$IDF_PATH` 替换为您实际的 ESP-IDF 安装路径)

3.  **设置目标芯片** (以 ESP32-S3 为例):
    ```bash
    idf.py set-target esp32s3
    ```

4.  **执行编译**:
    ```bash
    idf.py build
    ```
    编译成功后，固件及可执行文件将位于 `build/` 目录下。

### 4.3 烧录与监视

1.  **连接开发板**: 将您的 ESP32 开发板通过 USB 连接到电脑。

2.  **一键烧录和监视**: 使用以下命令来编译、烧录固件，并打开串口监视器查看日志输出。
    ```bash
    idf.py flash monitor
    ```

3.  **退出监视器**: 按 `Ctrl + ]` 组合键可以退出串口监视器。

### 4.4 仿真模式说明

-   本项目 ESP32 平台默认开启硬件在环仿真模式 (`UWB_DUAL_UART_SIM_MODE` 宏在 `include/uwb_mk8000.h` 中定义)。
-   此模式下，应用逻辑通过一个 UART (默认为 UART2) 与运行在同一芯片上的仿真器 (默认为 UART1) 通信，无需物理 UWB 模块即可进行测试。
-   如需在真实硬件上运行，请注释掉 `include/uwb_mk8000.h` 中的 `#define UWB_DUAL_UART_SIM_MODE 1` 宏。
