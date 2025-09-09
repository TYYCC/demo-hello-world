# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

# 编译模式，ON为接收模式（仅接收最小化构建），OFF为完整功能（打开其他任务）
option(EN_RECEIVER_MODE "Enable receiver-only minimal mode" ON)

if(EN_RECEIVER_MODE)
    message(STATUS "Receiver-only minimal mode is enabled. Build focuses on receiver functionality.")
    set(EXTRA_COMPONENT_DIRS "components/Receiver")
else()
    message(STATUS "Full feature mode - enabling Peripherals, LVGL and Receiver components")
    # 在完整功能模式下，同时包含外设、LVGL端口和接收器组件
    set(EXTRA_COMPONENT_DIRS "components/Peripherals" "components/lvgl_port" "components/Receiver")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(demo-hello-world)
