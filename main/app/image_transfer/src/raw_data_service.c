#include "raw_data_service.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char* TAG = "RAW_DATA_SERVICE";

// 全局状态变量
static bool s_raw_service_running = false;
static uint8_t* s_frame_buffer = NULL;
static size_t s_frame_size = 0;
static EventGroupHandle_t s_raw_event_group = NULL;
static raw_data_callback_t s_data_callback = NULL;
static void* s_callback_context = NULL;

#define RAW_DATA_READY_BIT (1 << 0)

// 处理原始数据
static void process_raw_data(const uint8_t* data, size_t length, void* context) {
    if (!s_raw_service_running || !s_data_callback) {
        return;
    }
    
    // 分配帧缓冲区（如果需要）
    if (!s_frame_buffer || s_frame_size < length) {
        if (s_frame_buffer) {
            free(s_frame_buffer);
        }
        s_frame_buffer = malloc(length);
        if (!s_frame_buffer) {
            ESP_LOGE(TAG, "Failed to allocate frame buffer");
            return;
        }
        s_frame_size = length;
    }
    
    // 复制数据到帧缓冲区
    memcpy(s_frame_buffer, data, length);
    
    // 设置数据就绪标志
    xEventGroupSetBits(s_raw_event_group, RAW_DATA_READY_BIT);
    
    // 调用数据回调
    s_data_callback(s_frame_buffer, length, s_callback_context);
}

// 初始化原始数据服务
esp_err_t raw_data_service_init(raw_data_callback_t data_callback, void* context) {
    if (s_raw_service_running) {
        ESP_LOGW(TAG, "Raw data service already running");
        return ESP_FAIL;
    }
    
    // 创建事件组
    s_raw_event_group = xEventGroupCreate();
    if (!s_raw_event_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }
    
    s_data_callback = data_callback;
    s_callback_context = context;
    s_raw_service_running = true;
    
    ESP_LOGI(TAG, "Raw data service initialized");
    return ESP_OK;
}

// 停止原始数据服务
void raw_data_service_deinit(void) {
    if (!s_raw_service_running) {
        return;
    }
    
    s_raw_service_running = false;
    
    // 释放帧缓冲区
    if (s_frame_buffer) {
        free(s_frame_buffer);
        s_frame_buffer = NULL;
        s_frame_size = 0;
    }
    
    // 删除事件组
    if (s_raw_event_group) {
        vEventGroupDelete(s_raw_event_group);
        s_raw_event_group = NULL;
    }
    
    s_data_callback = NULL;
    s_callback_context = NULL;
    
    ESP_LOGI(TAG, "Raw data service stopped");
}

// 获取原始数据服务状态
bool raw_data_service_is_running(void) {
    return s_raw_service_running;
}

// 获取事件组句柄
EventGroupHandle_t raw_data_service_get_event_group(void) {
    return s_raw_event_group;
}

// 获取最新帧数据
bool raw_data_service_get_latest_frame(uint8_t** frame_buffer, size_t* frame_size) {
    if (!s_raw_service_running || !s_frame_buffer || s_frame_size == 0) {
        return false;
    }
    
    *frame_buffer = s_frame_buffer;
    *frame_size = s_frame_size;
    return true;
}

// 帧数据解锁（允许新的数据写入）
void raw_data_service_frame_unlock(void) {
    if (s_raw_event_group) {
        xEventGroupClearBits(s_raw_event_group, RAW_DATA_READY_BIT);
    }
}