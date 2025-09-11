/*
 * @Author: tidycraze 2595256284@qq.com
 * @Date: 2025-09-10 13:37:58
 * @LastEditors: tidycraze 2595256284@qq.com
 * @LastEditTime: 2025-09-11 16:57:28
 * @FilePath: \demo-hello-world\main\app\image_transfer\src\image_transfer_app.c
 * @Description: 图像传输主应用实现，管理JPEG和LZ4解码服务
 * 
 */
#include "image_transfer_app.h"
#include "display_queue.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "jpeg_decoder_service.h"
#include "lz4_decoder_service.h"
#include "tcp_server_service.h"
#include <string.h>

static const char *TAG = "image_transfer_app";

// 全局状态变量
static image_transfer_mode_t s_current_mode = IMAGE_TRANSFER_MODE_JPEG;
static bool s_app_running = false;
static QueueHandle_t s_display_queue = NULL;

// JPEG解码器回调函数
static void jpeg_decoder_callback(const uint8_t* data, size_t length, 
                                 uint16_t width, uint16_t height, void* context) {
    // 这里可以处理解码后的数据，如果需要的话
    ESP_LOGD("image_transfer_app", "JPEG decoded: %dx%d, size: %d", width, height, length);
}

// 初始化图像传输应用程序
esp_err_t image_transfer_app_init(image_transfer_mode_t initial_mode) {
    if (s_app_running) {
        ESP_LOGW(TAG, "Image transfer app already running");
        return ESP_FAIL;
    }

    // 初始化显示队列
    s_display_queue = display_queue_init();
    if (s_display_queue == NULL) {
        ESP_LOGE(TAG, "Failed to initialize display queue");
        return ESP_FAIL;
    }

    // 初始化TCP服务器服务
    esp_err_t ret = tcp_server_service_init(6556);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize TCP server service");
        display_queue_deinit(s_display_queue);
        s_display_queue = NULL;
        return ret;
    }

    // 设置初始模式并初始化对应服务
    s_current_mode = initial_mode;
    s_app_running = true;
    ret = image_transfer_app_set_mode(initial_mode);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set initial mode: %d", initial_mode);
        s_app_running = false;
        tcp_server_service_deinit();
        display_queue_deinit(s_display_queue);
        s_display_queue = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "Image transfer app initialized with mode: %d", initial_mode);
    return ESP_OK;
}

// 设置图像传输模式
esp_err_t image_transfer_app_set_mode(image_transfer_mode_t mode) {
    if (!s_app_running) {
        ESP_LOGE(TAG, "App not running");
        return ESP_FAIL;
    }

    // 停止当前模式的服务
    switch (s_current_mode) {
    case IMAGE_TRANSFER_MODE_LZ4:
        lz4_decoder_service_deinit();
        break;
    case IMAGE_TRANSFER_MODE_JPEG:
        jpeg_decoder_service_deinit();
        break;
    case IMAGE_TRANSFER_MODE_TCP:
    case IMAGE_TRANSFER_MODE_UDP:
        // TCP/UDP模式不需要额外的清理
        break;
    }

    // 启动新模式的服务
    esp_err_t ret = ESP_OK;
    switch (mode) {
    case IMAGE_TRANSFER_MODE_LZ4:
        ESP_LOGI(TAG, "Initializing LZ4 decoder service");
        ret = lz4_decoder_service_init();
        break;
    case IMAGE_TRANSFER_MODE_JPEG:
        ret = jpeg_decoder_service_init(jpeg_decoder_callback, NULL, s_display_queue);
        break;
    case IMAGE_TRANSFER_MODE_TCP:
    case IMAGE_TRANSFER_MODE_UDP:
        // TCP/UDP模式不需要额外的初始化
        ret = ESP_OK;
        break;
    }

    if (ret == ESP_OK) {
        s_current_mode = mode;
        ESP_LOGI(TAG, "Mode changed to: %d", mode);
    } else {
        ESP_LOGE(TAG, "Failed to set mode: %d", mode);
    }

    return ret;
}

// 获取当前图像传输模式
image_transfer_mode_t image_transfer_app_get_mode(void) {
    return s_current_mode;
}

// 启动TCP服务器
esp_err_t image_transfer_app_start_tcp_server(uint16_t port) {
    if (!s_app_running) {
        ESP_LOGE(TAG, "App not running");
        return ESP_FAIL;
    }

    // TCP服务器在初始化时已经启动
    return ESP_OK;
}

// 停止TCP服务器
void image_transfer_app_stop_tcp_server(void) {
    tcp_server_service_deinit();
}

// 检查TCP服务器是否正在运行
bool image_transfer_app_is_tcp_server_running(void) {
    return tcp_server_service_is_running();
}

// 停止图像传输应用程序
void image_transfer_app_deinit(void) {
    if (!s_app_running) {
        return;
    }

    // 停止当前模式的服务
    switch (s_current_mode) {
    case IMAGE_TRANSFER_MODE_LZ4:
        lz4_decoder_service_deinit();
        break;
    case IMAGE_TRANSFER_MODE_JPEG:
        jpeg_decoder_service_deinit();
        break;
    case IMAGE_TRANSFER_MODE_TCP:
    case IMAGE_TRANSFER_MODE_UDP:
        // TCP/UDP模式不需要额外的清理
        break;
    }

    // 停止TCP服务器
    tcp_server_service_deinit();

    // 停止显示队列
    if (s_display_queue != NULL) {
        display_queue_deinit(s_display_queue);
        s_display_queue = NULL;
    }

    s_app_running = false;

    ESP_LOGI(TAG, "Image transfer app stopped");
}

// 检查应用程序是否正在运行
bool image_transfer_app_is_running(void) {
    return s_app_running;
}