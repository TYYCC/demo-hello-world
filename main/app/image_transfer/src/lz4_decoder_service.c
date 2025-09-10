#include "lz4_decoder_service.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "lz4.h"
#include <string.h>

static const char* TAG = "LZ4_DECODER_SERVICE";

// 全局状态变量
static bool s_lz4_service_running = false;
static uint8_t* s_compressed_buffer = NULL;
static uint8_t* s_decompressed_buffer = NULL;
static size_t s_max_compressed_size = 0;
static size_t s_max_decompressed_size = 0;
static EventGroupHandle_t s_lz4_event_group = NULL;
static lz4_decoder_callback_t s_data_callback = NULL;
static void* s_callback_context = NULL;

#define LZ4_DATA_READY_BIT (1 << 0)

// LZ4解压缩函数
static int lz4_decompress(const uint8_t* compressed_data, size_t compressed_size, 
                         uint8_t* decompressed_data, size_t max_decompressed_size) {
    int decompressed_size = LZ4_decompress_safe((const char*)compressed_data, 
                                                (char*)decompressed_data, 
                                                compressed_size, 
                                                max_decompressed_size);
    
    if (decompressed_size < 0) {
        ESP_LOGE(TAG, "LZ4 decompression failed: %d", decompressed_size);
        return -1;
    }
    
    return decompressed_size;
}

void lz4_decoder_service_process_data(const uint8_t* data, size_t length) {
    if (!s_lz4_service_running || !s_data_callback) {
        return;
    }
    
    // 确保压缩缓冲区足够大
    if (!s_compressed_buffer || s_max_compressed_size < length) {
        if (s_compressed_buffer) {
            free(s_compressed_buffer);
        }
        s_compressed_buffer = malloc(length);
        if (!s_compressed_buffer) {
            ESP_LOGE(TAG, "Failed to allocate compressed buffer");
            return;
        }
        s_max_compressed_size = length;
    }
    
    // 复制压缩数据
    memcpy(s_compressed_buffer, data, length);
    
    // 估计解压缩后的大小（LZ4压缩比通常为2:1到4:1）
    size_t estimated_decompressed_size = length * 4;
    
    // 确保解压缩缓冲区足够大
    if (!s_decompressed_buffer || s_max_decompressed_size < estimated_decompressed_size) {
        if (s_decompressed_buffer) {
            free(s_decompressed_buffer);
        }
        s_decompressed_buffer = malloc(estimated_decompressed_size);
        if (!s_decompressed_buffer) {
            ESP_LOGE(TAG, "Failed to allocate decompressed buffer");
            return;
        }
        s_max_decompressed_size = estimated_decompressed_size;
    }
    
    // 执行LZ4解压缩
    int decompressed_size = lz4_decompress(s_compressed_buffer, length, 
                                          s_decompressed_buffer, s_max_decompressed_size);
    
    if (decompressed_size > 0) {
        // 设置数据就绪标志
        xEventGroupSetBits(s_lz4_event_group, LZ4_DATA_READY_BIT);
        
        // 调用数据回调
        s_data_callback(s_decompressed_buffer, decompressed_size, s_callback_context);
    }
}

// 初始化LZ4解码服务
esp_err_t lz4_decoder_service_init(lz4_decoder_callback_t data_callback, void* context) {
    if (s_lz4_service_running) {
        ESP_LOGW(TAG, "LZ4 decoder service already running");
        return ESP_FAIL;
    }
    
    // 创建事件组
    s_lz4_event_group = xEventGroupCreate();
    if (!s_lz4_event_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }
    
    s_data_callback = data_callback;
    s_callback_context = context;
    s_lz4_service_running = true;
    
    ESP_LOGI(TAG, "LZ4 decoder service initialized");
    return ESP_OK;
}

// 停止LZ4解码服务
void lz4_decoder_service_deinit(void) {
    if (!s_lz4_service_running) {
        return;
    }
    
    s_lz4_service_running = false;
    
    // 释放缓冲区
    if (s_compressed_buffer) {
        free(s_compressed_buffer);
        s_compressed_buffer = NULL;
        s_max_compressed_size = 0;
    }
    
    if (s_decompressed_buffer) {
        free(s_decompressed_buffer);
        s_decompressed_buffer = NULL;
        s_max_decompressed_size = 0;
    }
    
    // 删除事件组
    if (s_lz4_event_group) {
        vEventGroupDelete(s_lz4_event_group);
        s_lz4_event_group = NULL;
    }
    
    s_data_callback = NULL;
    s_callback_context = NULL;
    
    ESP_LOGI(TAG, "LZ4 decoder service stopped");
}

// 获取LZ4解码服务状态
bool lz4_decoder_service_is_running(void) {
    return s_lz4_service_running;
}

// 获取事件组句柄
EventGroupHandle_t lz4_decoder_service_get_event_group(void) {
    return s_lz4_event_group;
}

// 获取最新解压缩数据
bool lz4_decoder_service_get_latest_frame(uint8_t** frame_buffer, size_t* frame_size) {
    if (!s_lz4_service_running || !s_decompressed_buffer || s_max_decompressed_size == 0) {
        return false;
    }
    
    *frame_buffer = s_decompressed_buffer;
    *frame_size = s_max_decompressed_size;
    return true;
}

// 帧数据解锁（允许新的数据写入）
void lz4_decoder_service_frame_unlock(void) {
    if (s_lz4_event_group) {
        xEventGroupClearBits(s_lz4_event_group, LZ4_DATA_READY_BIT);
    }
}