/*
 * @Author: tidycraze 2595256284@qq.com
 * @Date: 2025-09-10 13:37:58
 * @LastEditors: tidycraze 2595256284@qq.com
 * @LastEditTime: 2025-09-11 16:38:16
 * @FilePath: \demo-hello-world\main\app\image_transfer\src\lz4_decoder_service.c
 * @Description: LZ4解码服务实现，用于处理LZ4压缩图像数据
 * 
 */
#include "lz4_decoder_service.h"
#include "display_queue.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lz4.h"
#include "lz4frame.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "lz4_decoder";

// 全局状态变量
static TaskHandle_t s_lz4_decoder_task_handle = NULL;
EventGroupHandle_t lz4_decoder_event_group = NULL;
static SemaphoreHandle_t s_lz4_mutex = NULL;
static QueueHandle_t s_display_queue = NULL; // 显示队列句柄

// LZ4解压缩缓冲区
static uint8_t *s_compressed_buffer = NULL;
static uint8_t *s_decompressed_buffer = NULL;
static size_t s_max_compressed_size = 0;
static size_t s_max_decompressed_size = 0;
static size_t s_compressed_data_received = 0;

static void lz4_decoder_task(void *arg);

bool lz4_decoder_service_init(QueueHandle_t display_queue) {
    // 创建事件组
    lz4_decoder_event_group = xEventGroupCreate();
    if (lz4_decoder_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return false;
    }

    // 创建互斥锁
    s_lz4_mutex = xSemaphoreCreateMutex();
    if (s_lz4_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        vEventGroupDelete(lz4_decoder_event_group);
        lz4_decoder_event_group = NULL;
        return false;
    }

    // 分配压缩数据缓冲区（最大1MB）
    s_compressed_buffer = heap_caps_malloc(1024 * 1024, MALLOC_CAP_SPIRAM);
    if (s_compressed_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate compressed buffer");
        vSemaphoreDelete(s_lz4_mutex);
        vEventGroupDelete(lz4_decoder_event_group);
        s_lz4_mutex = NULL;
        lz4_decoder_event_group = NULL;
        return false;
    }
    s_max_compressed_size = 1024 * 1024;

    // 分配解压缩数据缓冲区（最大2MB）
    s_decompressed_buffer = heap_caps_malloc(2 * 1024 * 1024, MALLOC_CAP_SPIRAM);
    if (s_decompressed_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate decompressed buffer");
        heap_caps_free(s_compressed_buffer);
        vSemaphoreDelete(s_lz4_mutex);
        vEventGroupDelete(lz4_decoder_event_group);
        s_compressed_buffer = NULL;
        s_lz4_mutex = NULL;
        lz4_decoder_event_group = NULL;
        return false;
    }
    s_max_decompressed_size = 2 * 1024 * 1024;

    // 保存显示队列句柄
    s_display_queue = display_queue;

    // 创建解码器任务
    BaseType_t result = xTaskCreate(
        lz4_decoder_task,
        "lz4_decoder",
        4096,
        NULL,
        5,
        &s_lz4_decoder_task_handle
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LZ4 decoder task");
        heap_caps_free(s_compressed_buffer);
        heap_caps_free(s_decompressed_buffer);
        vSemaphoreDelete(s_lz4_mutex);
        vEventGroupDelete(lz4_decoder_event_group);
        s_compressed_buffer = NULL;
        s_decompressed_buffer = NULL;
        s_lz4_mutex = NULL;
        lz4_decoder_event_group = NULL;
        return false;
    }

    ESP_LOGI(TAG, "LZ4 decoder service initialized");
    return true;
}

void lz4_decoder_service_deinit(void) {
    // 发送停止信号
    if (lz4_decoder_event_group) {
        xEventGroupSetBits(lz4_decoder_event_group, LZ4_DECODER_STOP_BIT);
    }

    // 等待任务结束
    if (s_lz4_decoder_task_handle) {
        vTaskDelete(s_lz4_decoder_task_handle);
        s_lz4_decoder_task_handle = NULL;
    }

    // 清理资源
    if (s_compressed_buffer) {
        heap_caps_free(s_compressed_buffer);
        s_compressed_buffer = NULL;
    }

    if (s_decompressed_buffer) {
        heap_caps_free(s_decompressed_buffer);
        s_decompressed_buffer = NULL;
    }

    if (s_lz4_mutex) {
        vSemaphoreDelete(s_lz4_mutex);
        s_lz4_mutex = NULL;
    }

    if (lz4_decoder_event_group) {
        vEventGroupDelete(lz4_decoder_event_group);
        lz4_decoder_event_group = NULL;
    }

    ESP_LOGI(TAG, "LZ4 decoder service deinitialized");
}

bool lz4_decoder_service_is_running(void) {
    return s_lz4_decoder_task_handle != NULL;
}

EventGroupHandle_t lz4_decoder_service_get_event_group(void) {
    return lz4_decoder_event_group;
}

void lz4_decoder_service_frame_unlock(void) {
    // LZ4解码器不需要帧解锁功能，保留接口兼容性
}

void lz4_decoder_service_process_data(const uint8_t *data, uint32_t data_len) {
    if (xSemaphoreTake(s_lz4_mutex, portMAX_DELAY) == pdTRUE) {
        // 检查是否有足够空间
        if (s_compressed_data_received + data_len > s_max_compressed_size) {
            ESP_LOGE(TAG, "LZ4 compressed data buffer overflow");
            xSemaphoreGive(s_lz4_mutex);
            return;
        }

        // 复制数据到缓冲区
        memcpy(s_compressed_buffer + s_compressed_data_received, data, data_len);
        s_compressed_data_received += data_len;

        // 发送数据就绪信号
        xEventGroupSetBits(lz4_decoder_event_group, LZ4_DECODER_DATA_READY_BIT);

        xSemaphoreGive(s_lz4_mutex);
    }
}

static void lz4_decoder_task(void *arg) {
    ESP_LOGI(TAG, "LZ4 decoder task started");

    while (1) {
        // 等待数据就绪或停止信号
        EventBits_t bits = xEventGroupWaitBits(
            lz4_decoder_event_group,
            LZ4_DECODER_DATA_READY_BIT | LZ4_DECODER_STOP_BIT,
            pdTRUE,
            pdFALSE,
            portMAX_DELAY
        );

        if (bits & LZ4_DECODER_STOP_BIT) {
            break;
        }

        if (bits & LZ4_DECODER_DATA_READY_BIT) {
            if (xSemaphoreTake(s_lz4_mutex, portMAX_DELAY) == pdTRUE) {
                // 执行LZ4帧解压缩
                LZ4F_decompressionContext_t dctx = NULL;
                LZ4F_errorCode_t errorCode;

                // 创建解压缩上下文
                errorCode = LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION);
                if (LZ4F_isError(errorCode)) {
                    ESP_LOGE(TAG, "LZ4F_createDecompressionContext failed: %s", LZ4F_getErrorName(errorCode));
                    xSemaphoreGive(s_lz4_mutex);
                    continue;
                }

                // 设置输入和输出缓冲区
                LZ4F_decompressOptions_t options = {0};
                size_t srcSize = s_compressed_data_received;
                size_t dstSize = s_max_decompressed_size;

                // LZ4帧解压缩
                size_t result = LZ4F_decompress(dctx, s_decompressed_buffer, &dstSize,
                                               s_compressed_buffer, &srcSize, &options);

                if (LZ4F_isError(result)) {
                    ESP_LOGE(TAG, "LZ4F_decompress failed: %s", LZ4F_getErrorName(result));
                } else if (dstSize > 0) {
                    // 假设解压缩数据为RGB565格式，转换为RGB565LE
                    size_t pixels = dstSize / 2;
                    uint16_t *src = (uint16_t *)s_decompressed_buffer;
                    
                    // 分配LE缓冲区
                    uint8_t *le_buffer = heap_caps_malloc(dstSize, MALLOC_CAP_SPIRAM);
                    if (le_buffer) {
                        uint16_t *dst = (uint16_t *)le_buffer;
                        for (size_t i = 0; i < pixels; i++) {
                            uint16_t pixel = src[i];
                            dst[i] = (pixel >> 8) | (pixel << 8); // 字节序转换
                        }

                        // 创建帧消息并推送到显示队列
                        frame_msg_t frame_msg = {
                            .type = FRAME_TYPE_LZ4,
                            .width = 240,  // 假设标准尺寸
                            .height = 180, // 假设标准尺寸
                            .payload_len = dstSize,
                            .frame_buffer = le_buffer
                        };

                        // 使用保存的显示队列句柄推送帧
                        if (s_display_queue) {
                            display_queue_enqueue(s_display_queue, &frame_msg);
                        } else {
                            heap_caps_free(le_buffer);
                        }
                    }
                }

                // 清理上下文
                LZ4F_freeDecompressionContext(dctx);

                // 重置数据缓冲区
                s_compressed_data_received = 0;

                xSemaphoreGive(s_lz4_mutex);
            }
        }
    }

    ESP_LOGI(TAG, "LZ4 decoder task stopped");
    vTaskDelete(NULL);
}