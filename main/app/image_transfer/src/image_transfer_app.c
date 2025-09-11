#include "image_transfer_app.h"
#include "tcp_server_service.h"
#include "raw_data_service.h"
#include "lz4_decoder_service.h"
#include "jpeg_decoder_service.h"
#include "ui_mapping_service.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char* TAG = "IMAGE_TRANSFER_APP";

// 全局状态变量
static image_transfer_mode_t s_current_mode = IMAGE_TRANSFER_MODE_RAW;
static bool s_app_running = false;

// UI数据回调函数
static void ui_data_callback(const uint8_t* data, size_t length,
                           uint16_t width, uint16_t height,
                           ui_data_type_t data_type, void* context) {
    // 根据不同的数据类型处理UI显示
    switch (data_type) {
        case UI_DATA_TYPE_RAW:
            ESP_LOGD(TAG, "Raw data received: %zu bytes", length);
            break;
        case UI_DATA_TYPE_LZ4:
            ESP_LOGD(TAG, "LZ4 decompressed data: %zu bytes", length);
            // 调用UI模块更新图像（复用JPEG的更新函数）
            extern void ui_image_transfer_update_jpeg_frame(const uint8_t* data, size_t length, uint16_t width, uint16_t height);
            // 对于LZ4数据，我们明确指定固定分辨率
            ui_image_transfer_update_jpeg_frame(data, length, 240, 180); // LZ4数据默认分辨率
            break;
        case UI_DATA_TYPE_JPEG:
            ESP_LOGD(TAG, "JPEG decoded data: %zu bytes, %dx%d", length, width, height);
            // 调用UI模块更新图像
            extern void ui_image_transfer_update_jpeg_frame(const uint8_t* data, size_t length, uint16_t width, uint16_t height);
            ui_image_transfer_update_jpeg_frame(data, length, width, height);
            break;
    }

    // 解锁帧数据，允许新的数据写入
    ui_mapping_service_frame_unlock();
}

// 初始化图像传输应用程序
esp_err_t image_transfer_app_init(image_transfer_mode_t initial_mode) {
    if (s_app_running) {
        ESP_LOGW(TAG, "Image transfer app already running");
        return ESP_FAIL;
    }
    
    // 初始化UI映射服务
    esp_err_t ret = ui_mapping_service_init(ui_data_callback, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize UI mapping service");
        return ret;
    }
    
    // 初始化TCP服务器服务
    ret = tcp_server_service_init(6556);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize TCP server service");
        ui_mapping_service_deinit();
        return ret;
    }

    // 设置初始模式并初始化对应服务
    s_current_mode = initial_mode;
    s_app_running = true;  // 先设置运行状态，供set_mode使用
    ret = image_transfer_app_set_mode(initial_mode);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set initial mode: %d", initial_mode);
        s_app_running = false;  // 失败时重置状态
        tcp_server_service_deinit();
        ui_mapping_service_deinit();
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
        case IMAGE_TRANSFER_MODE_RAW:
            raw_data_service_deinit();
            break;
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
        case IMAGE_TRANSFER_MODE_RAW:
            ret = raw_data_service_init(ui_mapping_service_get_raw_callback(), NULL);
            break;
        case IMAGE_TRANSFER_MODE_LZ4:
            ESP_LOGI(TAG, "Initializing LZ4 decoder service");
            ret = lz4_decoder_service_init(ui_mapping_service_get_lz4_callback(), NULL);
            ESP_LOGI(TAG, "LZ4 decoder service init result: %d", ret);
            break;
        case IMAGE_TRANSFER_MODE_JPEG:
            ret = jpeg_decoder_service_init(ui_mapping_service_get_jpeg_callback(), NULL);
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
        case IMAGE_TRANSFER_MODE_RAW:
            raw_data_service_deinit();
            break;
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
    
    // 停止UI映射服务
    ui_mapping_service_deinit();
    
    s_app_running = false;
    
    ESP_LOGI(TAG, "Image transfer app stopped");
}

// 检查应用程序是否正在运行
bool image_transfer_app_is_running(void) {
    return s_app_running;
}