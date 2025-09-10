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
static TaskHandle_t s_jpeg_decode_task_handle = NULL;

#define JPEG_DATA_READY_BIT (1 << 0)

// JPEG解码任务函数
static void jpeg_decode_task(void* pvParameters) {
    while (s_jpeg_service_running) {
        // 等待数据就绪
        EventBits_t bits = xEventGroupWaitBits(s_jpeg_event_group,
                                             JPEG_DATA_READY_BIT,
                                             pdTRUE,  // 清除标志
                                             pdFALSE, // 等待所有位
                                             portMAX_DELAY);
        
        if (bits & JPEG_DATA_READY_BIT) {
            // 这里应该有真正的JPEG解码逻辑
            // 暂时模拟解码过程，添加适当延迟
            vTaskDelay(pdMS_TO_TICKS(10)); // 模拟解码时间
            
            // 调用数据回调
            if (s_data_callback && s_jpeg_buffer && s_decoded_buffer) {
                // 假设解码后的数据尺寸与原始数据相同
                s_data_callback(s_decoded_buffer, s_max_jpeg_size, 0, 0, s_callback_context);
            }
        }
    }
    
    vTaskDelete(NULL);
}

void jpeg_decoder_service_process_data(const uint8_t* data, size_t length) {
    if (!s_jpeg_service_running || !s_data_callback) {
        return;
    }

    // 确保JPEG缓冲区足够大
    if (!s_jpeg_buffer || s_max_jpeg_size < length) {
        if (s_jpeg_buffer) {
            free(s_jpeg_buffer);
        }
        s_jpeg_buffer = malloc(length);
        if (!s_jpeg_buffer) {
            ESP_LOGE(TAG, "Failed to allocate JPEG buffer");
            return;
        }
        s_max_jpeg_size = length;
    }

    // 复制JPEG数据
    memcpy(s_jpeg_buffer, data, length);

    // 假设最大解码尺寸为原始尺寸的4倍，或者根据实际情况调整
    size_t estimated_decoded_size = length * 4; 

    // 确保解码缓冲区足够大
    if (!s_decoded_buffer || s_max_decoded_size < estimated_decoded_size) {
        if (s_decoded_buffer) {
            free(s_decoded_buffer);
        }
        s_decoded_buffer = malloc(estimated_decoded_size);
        if (!s_decoded_buffer) {
            ESP_LOGE(TAG, "Failed to allocate decoded buffer");
            return;
        }
        s_max_decoded_size = estimated_decoded_size;
    }

    // 设置数据就绪标志
    xEventGroupSetBits(s_jpeg_event_group, JPEG_DATA_READY_BIT);
    
    // 注意：这里不立即调用回调，而是等待真正的解码任务来处理数据
    // 真正的解码应该在单独的任务中完成，避免阻塞TCP接收
}


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
    
    // 创建JPEG解码任务
    BaseType_t result = xTaskCreate(
        jpeg_decode_task,
        "jpeg_decode",
        4096,  // 堆栈大小
        NULL,
        5,     // 优先级
        &s_jpeg_decode_task_handle
    );
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create JPEG decode task");
        vEventGroupDelete(s_jpeg_event_group);
        s_jpeg_event_group = NULL;
        s_jpeg_service_running = false;
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "JPEG decoder service initialized with decode task");
    return ESP_OK;
}

// 停止JPEG解码服务
void jpeg_decoder_service_deinit(void) {
    if (!s_jpeg_service_running) {
        return;
    }
    
    s_jpeg_service_running = false;
    
    // 通知解码任务退出
    if (s_jpeg_event_group) {
        xEventGroupSetBits(s_jpeg_event_group, JPEG_DATA_READY_BIT);
    }
    
    // 等待解码任务结束
    if (s_jpeg_decode_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(100)); // 给任务一些时间退出
        s_jpeg_decode_task_handle = NULL;
    }
    
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