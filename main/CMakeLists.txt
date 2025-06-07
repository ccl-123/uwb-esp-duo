# CMakeLists.txt in 'main' for the UWB Refactor Example
#
# This file tells the ESP-IDF build system how to compile the application,
# including the UWB driver source files, HAL implementation, and simulator,
# using relative paths for robustness.

idf_component_register(
    SRCS
        "main.c"
        "../src/uwb_core.c"
        "../src/uwb_at.c"
        "../hal/esp32/hal_esp32.c"
        "../simulator/uwb_module_simulator.c"
    INCLUDE_DIRS
        "."
        "../include"
        "../hal/include"
        "../hal/esp32/include"
        "../simulator/include"
    REQUIRES
        "nvs_flash"
        "driver"
)
