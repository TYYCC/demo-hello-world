/*
 * @Author: tidycraze 2595256284@qq.com
 * @Date: 2025-07-28 11:29:59
 * @LastEditors: tidycraze 2595256284@qq.com
 * @LastEditTime: 2025-09-09 16:33:30
 * @FilePath: \demo-hello-world\main\main.c
 * @Description: 主函数入口
 *
 */

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
#include "Arduino.h"

static const char* TAG = "main";

extern "C" {
    extern void elrs_rx_loop();
    extern void elrs_rx_setup();
}

static bool system_init_complete = false;

static void rx_setup(void* pvParameters)
{
    esp_log_level_t original_level = esp_log_level_get("*");
    esp_log_level_set("*", ESP_LOG_WARN);

    initArduino();
    
    // 初始化 Arduino Serial 用于 ELRS 日志输出
    Serial.begin(115200);
    Serial.setDebugOutput(true);  // 允许调试输出

    delay(100);  // 等待串口稳定
    
    Serial.println("\n[ELRS] Serial initialized for ELRS debug output");
    
    // Wait for system initialization to complete
    while (!system_init_complete) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // // 初始化Elrs接收器核心
    elrs_rx_setup();

    while (1)
    {
        elrs_rx_loop();
    }
    
}
static void system_init_task(void* pvParameters)
{
    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
    }

    // 初始化 SPI 从机并启动接收任务
    if (spi_receiver_init() == ESP_OK) {
        spi_receiver_start();
    } else {
        ESP_LOGE(TAG, "Failed to initialize SPI Receiver");
    }

    // Mark system initialization as complete
    system_init_complete = true;

    // Initialization complete

    // // 初始化WiFi配对管理器
    // wifi_pairing_config_t wifi_config = {
    //     .scan_interval_ms = 1000,
    //     .task_priority = 3,
    //     .task_stack_size = 4096,
    //     .connection_timeout_ms = 10000,
    //     .target_ssid_prefix = "tidy_",
    //     .default_password = "22989822",
    // };
    // // 启动带WIFI事件集成的TCP管理器
    // esp_err_t tcp_result = tcp_task_manager_start_with_wifi(&wifi_config);
    // if (tcp_result != ESP_OK) {
    //     ESP_LOGE(TAG, "Failed to start TCP manager: %s", esp_err_to_name(tcp_result));
    // } else {
    //     ESP_LOGI(TAG, "Event-driven TCP manager started successfully");
    // }

    // 启动TCP服务器
    // tcp_server_start();

    while (1) {
        ESP_LOGI(TAG, "System running, free heap: %lu bytes",
                 (unsigned long)esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

extern "C" void app_main(void) {
    // Create system init task on core 1
    // xTaskCreatePinnedToCore(system_init_task, "sys_init", 4096,
    //                         NULL, 0, NULL, 1);
    
    // Create ELRS RX task on core 1 as well
    // All tasks run on core 1 to avoid multi-core FreeRTOS spinlock issues
    xTaskCreatePinnedToCore(rx_setup, "elrs_rx", 32768,
                            NULL, 1, NULL, 1);
}
