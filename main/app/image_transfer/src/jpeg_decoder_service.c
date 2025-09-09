#include "jpeg_decoder_service.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_jpeg_common.h"
#include "esp_jpeg_dec.h"
#include <string.h>

static const char* TAG = "JPEG_DECODER_SERVICE";

// 全局状态变量
static bool s_jpeg_service_running = false;
static uint8_t* s_jpeg_buffer = NULL;
static uint8_t* s_decoded_buffer = NULL;
static size_t s_max_jpeg_size = 0;
static size_t s_max_decoded_size = 0;
static EventGroupHandle_t s_jpeg_event_group = NULL;
static jpeg_decoder_callback_t s_data_callback = NULL;
static void* s_callback_context = NULL;

#define JPEG_DATA_READY_BIT (1 << 0)



// 处理JPEG数据


// 初始化JPEG解码服务
esp_err_t jpeg_decoder_service_init(jpeg_decoder_callback_t data_callback, void* context) {
    if (s_jpeg_service_running) {
        ESP_LOGW(TAG, "JPEG decoder service already running");
        return ESP_FAIL;
    }
    
    // 创建事件组
    s_jpeg_event_group = xEventGroupCreate();
    if (!s_jpeg_event_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }
    
    s_data_callback = data_callback;
    s_callback_context = context;
    s_jpeg_service_running = true;
    
    ESP_LOGI(TAG, "JPEG decoder service initialized");
    return ESP_OK;
}

// 停止JPEG解码服务
void jpeg_decoder_service_deinit(void) {
    if (!s_jpeg_service_running) {
        return;
    }
    
    s_jpeg_service_running = false;
    
    // 释放缓冲区
    if (s_jpeg_buffer) {
        free(s_jpeg_buffer);
        s_jpeg_buffer = NULL;
        s_max_jpeg_size = 0;
    }
    
    if (s_decoded_buffer) {
        free(s_decoded_buffer);
        s_decoded_buffer = NULL;
        s_max_decoded_size = 0;
    }
    
    // 删除事件组
    if (s_jpeg_event_group) {
        vEventGroupDelete(s_jpeg_event_group);
        s_jpeg_event_group = NULL;
    }
    
    s_data_callback = NULL;
    s_callback_context = NULL;
    
    ESP_LOGI(TAG, "JPEG decoder service stopped");
}

// 获取JPEG解码服务状态
bool jpeg_decoder_service_is_running(void) {
    return s_jpeg_service_running;
}

// 获取事件组句柄
EventGroupHandle_t jpeg_decoder_service_get_event_group(void) {
    return s_jpeg_event_group;
}

// 帧数据解锁（允许新的数据写入）
void jpeg_decoder_service_frame_unlock(void) {
    if (s_jpeg_event_group) {
        xEventGroupClearBits(s_jpeg_event_group, JPEG_DATA_READY_BIT);
    }
}