/*
 * @Author: tidycraze 2595256284@qq.com
 * @Date: 2025-07-28 11:29:59
 * @LastEditors: tidycraze 2595256284@qq.com
 * @LastEditTime: 2025-09-09 16:33:30
 * @FilePath: \demo-hello-world\main\main.c
 * @Description: 主函数入口
 *
 */

#if EN_RECEIVER_MODE

#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include <inttypes.h>
#include "esp_heap_caps.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "spi_slave_receiver.h"
#include "usb_device_receiver.h"
#include "led_status_manager.h"
#include "wifi_pairing_manager.h"
#include "task.h"
#include "tcp_server.h"

static const char* TAG = "main";

static void log_heap_info(const char* step) {
    ESP_LOGI(TAG, "Heap info at step '%s':", step);
    ESP_LOGI(TAG, "  Internal RAM free: %u bytes", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "  PSRAM free: %u bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

void app_main(void) {

    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
        return;
    }

    log_heap_info("Initial");

    // 初始化LED管理器
    led_manager_config_t led_manager_config = {
        .led_count = 1, .queue_size = 1, .task_priority = 2, .task_stack_size = 2048};
    if (led_status_manager_init(&led_manager_config) == ESP_OK) {
        led_status_set_style(LED_STYLE_RED_SOLID, LED_PRIORITY_LOW, 0);
        log_heap_info("After LED Manager Init");
    } else {
        ESP_LOGE(TAG, "Failed to initialize LED Status Manager");
    }

    // 初始化 SPI 从机并启动接收任务
    if (spi_receiver_init() == ESP_OK) {
        spi_receiver_start();
        log_heap_info("After SPI Receiver Init");
    } else {
        ESP_LOGE(TAG, "Failed to initialize SPI Receiver");
    }

    // 初始化 USB CDC 从机并启动接收任务
    ESP_LOGI(TAG, "开始初始化USB Receiver");
    if (usb_receiver_init() == ESP_OK) {
        usb_receiver_start();
        log_heap_info("After USB Receiver Init");
    } else {
        ESP_LOGE(TAG, "Failed to initialize USB Receiver");
    }

    // 启动事件驱动的TCP管理器
    ESP_LOGI(TAG, "启动事件驱动TCP管理器");

    // 初始化WiFi配对管理器
    wifi_pairing_config_t wifi_config = {
        .scan_interval_ms = 1000,
        .task_priority = 3,
        .task_stack_size = 4096,
        .connection_timeout_ms = 10000,
        .target_ssid_prefix = "tidy_",
        .default_password = "22989822",
    };
    // 启动带WIFI事件集成的TCP管理器
    esp_err_t tcp_result = tcp_task_manager_start_with_wifi(&wifi_config);
    if (tcp_result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start TCP manager: %s", esp_err_to_name(tcp_result));
    } else {
        ESP_LOGI(TAG, "Event-driven TCP manager started successfully");
        log_heap_info("After TCP Manager Init");
    }

    // 启动TCP服务器
    tcp_server_start();

    while (1) {
        ESP_LOGI(TAG, "Receiver running, free heap: %lu bytes",
                 (unsigned long)esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
#else

// ESP-IDF 系统头文件必须在 FreeRTOS 头文件之前
#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_system.h"
#include "sdkconfig.h"
#include <inttypes.h>
#include <stdio.h>

#include "Arduino.h"

// FreeRTOS 头文件
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 项目组件头文件
#include "task_init.h"

static const char* TAG = "MAIN";

extern "C" {
extern esp_err_t components_init(void);
extern void elrs_setup(void);
extern void elrs_loop(void);
}

extern "C" void app_main(void) {

    initArduino();
    elrs_setup();
    elrs_loop();
    // 初始化所有组件
    esp_err_t ret = components_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize components: %s", esp_err_to_name(ret));
        return;
    }

    // 初始化任务管理
    ret = init_all_tasks();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize tasks: %s", esp_err_to_name(ret));
        return;
    }

    // 显示当前运行的任务
    vTaskDelay(pdMS_TO_TICKS(1000)); // 等待任务启动
    list_running_tasks();

    // 主任务进入轻量级监控循环
    while (1) {
        ESP_LOGI(TAG, "Main loop: System running normally, free heap: %lu bytes",
                 (unsigned long)esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(30000)); // 30秒打印一次状态
    }
}

#endif
