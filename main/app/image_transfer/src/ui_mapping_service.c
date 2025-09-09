#include "ui_mapping_service.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char* TAG = "UI_MAPPING_SERVICE";

// 全局状态变量
static bool s_ui_mapping_running = false;
static ui_mapping_callback_t s_ui_callback = NULL;
static void* s_ui_callback_context = NULL;
static EventGroupHandle_t s_ui_event_group = NULL;

#define UI_DATA_READY_BIT (1 << 0)

// 处理原始数据到UI的映射
static void map_raw_data_to_ui(const uint8_t* data, size_t length, void* context) {
    if (!s_ui_mapping_running || !s_ui_callback) {
        return;
    }
    
    // 设置数据就绪标志
    xEventGroupSetBits(s_ui_event_group, UI_DATA_READY_BIT);
    
    // 调用UI回调，传递原始数据
    s_ui_callback(data, length, 0, 0, UI_DATA_TYPE_RAW, s_ui_callback_context);
}

// 处理LZ4解压缩数据到UI的映射
static void map_lz4_data_to_ui(const uint8_t* data, size_t length, void* context) {
    if (!s_ui_mapping_running || !s_ui_callback) {
        return;
    }
    
    // 设置数据就绪标志
    xEventGroupSetBits(s_ui_event_group, UI_DATA_READY_BIT);
    
    // 调用UI回调，传递解压缩数据
    s_ui_callback(data, length, 0, 0, UI_DATA_TYPE_LZ4, s_ui_callback_context);
}

// 处理JPEG解码数据到UI的映射
static void map_jpeg_data_to_ui(const uint8_t* data, size_t length, 
                               uint16_t width, uint16_t height, void* context) {
    if (!s_ui_mapping_running || !s_ui_callback) {
        return;
    }
    
    // 设置数据就绪标志
    xEventGroupSetBits(s_ui_event_group, UI_DATA_READY_BIT);
    
    // 调用UI回调，传递JPEG解码数据
    s_ui_callback(data, length, width, height, UI_DATA_TYPE_JPEG, s_ui_callback_context);
}

// 初始化UI映射服务
esp_err_t ui_mapping_service_init(ui_mapping_callback_t ui_callback, void* context) {
    if (s_ui_mapping_running) {
        ESP_LOGW(TAG, "UI mapping service already running");
        return ESP_FAIL;
    }
    
    // 创建事件组
    s_ui_event_group = xEventGroupCreate();
    if (!s_ui_event_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }
    
    s_ui_callback = ui_callback;
    s_ui_callback_context = context;
    s_ui_mapping_running = true;
    
    ESP_LOGI(TAG, "UI mapping service initialized");
    return ESP_OK;
}

// 停止UI映射服务
void ui_mapping_service_deinit(void) {
    if (!s_ui_mapping_running) {
        return;
    }
    
    s_ui_mapping_running = false;
    
    // 删除事件组
    if (s_ui_event_group) {
        vEventGroupDelete(s_ui_event_group);
        s_ui_event_group = NULL;
    }
    
    s_ui_callback = NULL;
    s_ui_callback_context = NULL;
    
    ESP_LOGI(TAG, "UI mapping service stopped");
}

// 获取UI映射服务状态
bool ui_mapping_service_is_running(void) {
    return s_ui_mapping_running;
}

// 获取事件组句柄
EventGroupHandle_t ui_mapping_service_get_event_group(void) {
    return s_ui_event_group;
}

// 获取原始数据映射回调
raw_data_callback_t ui_mapping_service_get_raw_callback(void) {
    return map_raw_data_to_ui;
}

// 获取LZ4数据映射回调
lz4_decoder_callback_t ui_mapping_service_get_lz4_callback(void) {
    return map_lz4_data_to_ui;
}

// 获取JPEG数据映射回调
jpeg_decoder_callback_t ui_mapping_service_get_jpeg_callback(void) {
    return map_jpeg_data_to_ui;
}

// 帧数据解锁（允许新的数据写入）
void ui_mapping_service_frame_unlock(void) {
    if (s_ui_event_group) {
        xEventGroupClearBits(s_ui_event_group, UI_DATA_READY_BIT);
    }
}