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
#include "logging.h"
#include <cstdio>
#include <Stream.h>

// Simple wrapper stream that writes to stdout (USB-Serial-JTAG)
class USBStream : public Stream {
public:
    // Write a single character
    size_t write(uint8_t c) override {
        putchar(c);
        return 1;
    }

    // Write multiple bytes
    size_t write(const uint8_t *buffer, size_t size) override {
        for (size_t i = 0; i < size; i++) {
            putchar(buffer[i]);
        }
        return size;
    }

    // Read is not supported
    int read() override {
        return -1;
    }

    int peek() override {
        return -1;
    }

    int available() override {
        return 0;
    }

    void flush() override {
        fflush(stdout);
    }
};

// Global USB stream instance
USBStream usbStream;

static const char* TAG = "main";

extern "C" {
    extern void elrs_rx_loop();
    extern void elrs_rx_setup();
}

static void rx_setup(void* pvParameters)
{
    initArduino();
    delay(100);
    BackpackOrLogStrm = &usbStream;
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
    // Initialize NVS first
    esp_err_t ret = nvs_flash_init();
    ESP_LOGI("NVS", "init = %s", esp_err_to_name(ret));
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Create ELRS RX task on core 0 (same as main)
    xTaskCreatePinnedToCore(rx_setup, "elrs_rx", 32768,
                            NULL, 1, NULL, 0);
}
