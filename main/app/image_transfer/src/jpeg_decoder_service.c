#include "jpeg_decoder_service.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_jpeg_common.h"
#include "esp_jpeg_dec.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdlib.h>

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
    // 初始化JPEG解码器
    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    config.output_type = JPEG_PIXEL_FORMAT_RGB565_LE; // ESP32 LVGL使用RGB565格式

    jpeg_error_t dec_ret;
    jpeg_dec_handle_t jpeg_dec = NULL;
    dec_ret = jpeg_dec_open(&config, &jpeg_dec);
    if (dec_ret != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "Failed to open JPEG decoder: %d", dec_ret);
        s_jpeg_decode_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "JPEG decode task started with hardware decoder");

    while (s_jpeg_service_running) {
        // 等待数据就绪
        EventBits_t bits = xEventGroupWaitBits(s_jpeg_event_group,
                                             JPEG_DATA_READY_BIT,
                                             pdTRUE,  // 清除标志
                                             pdFALSE, // 等待所有位
                                             pdMS_TO_TICKS(100)); // 100ms超时，避免永久阻塞

        if (bits & JPEG_DATA_READY_BIT) {
            ESP_LOGI(TAG, "JPEG decode task: received data ready signal");
            if (s_jpeg_buffer && s_max_jpeg_size > 0) {
                ESP_LOGI(TAG, "JPEG decode task: buffer valid, size=%zu", s_max_jpeg_size);
                // 解析JPEG头部信息
                jpeg_dec_io_t* jpeg_io = calloc(1, sizeof(jpeg_dec_io_t));
                jpeg_dec_header_info_t* out_info = calloc(1, sizeof(jpeg_dec_header_info_t));

                if (jpeg_io && out_info) {
                    // 设置输入缓冲区
                    jpeg_io->inbuf = s_jpeg_buffer;
                    jpeg_io->inbuf_len = s_max_jpeg_size;

                    // 解析JPEG头部
                    ESP_LOGI(TAG, "JPEG decode task: parsing header...");
                    dec_ret = jpeg_dec_parse_header(jpeg_dec, jpeg_io, out_info);
                    if (dec_ret == JPEG_ERR_OK) {
                        ESP_LOGI(TAG, "JPEG header parsed successfully: %dx%d", out_info->width, out_info->height);

                        // 确保解码缓冲区足够大
                        size_t required_size = out_info->width * out_info->height * 2; // RGB565 = 2字节/像素
                        if (!s_decoded_buffer || s_max_decoded_size < required_size) {
                            if (s_decoded_buffer) {
                                free(s_decoded_buffer);
                            }
                            s_decoded_buffer = heap_caps_aligned_alloc(16, required_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                            if (!s_decoded_buffer) {
                                ESP_LOGE(TAG, "Failed to allocate decoded buffer");
                                free(jpeg_io);
                                free(out_info);
                                continue;
                            }
                            s_max_decoded_size = required_size;
                        }

                        // 设置输出缓冲区
                        jpeg_io->outbuf = s_decoded_buffer;

                        // 执行JPEG解码
                        ESP_LOGI(TAG, "JPEG decode task: processing decode...");
                        dec_ret = jpeg_dec_process(jpeg_dec, jpeg_io);
                        if (dec_ret == JPEG_ERR_OK) {
                            ESP_LOGI(TAG, "JPEG decoded successfully: %dx%d", out_info->width, out_info->height);

                            // 调用数据回调，传递解码后的数据
                            if (s_data_callback) {
                                ESP_LOGI(TAG, "JPEG decode task: calling data callback");
                                s_data_callback(s_decoded_buffer, required_size,
                                              out_info->width, out_info->height, s_callback_context);
                            } else {
                                ESP_LOGW(TAG, "JPEG decode task: no data callback set");
                            }
                        } else {
                            ESP_LOGE(TAG, "JPEG decode failed: %d", dec_ret);
                        }
                    } else {
                        ESP_LOGE(TAG, "JPEG header parse failed: %d", dec_ret);
                    }
                } else {
                    ESP_LOGE(TAG, "Failed to allocate JPEG decode structures");
                }

                // 清理临时结构
                if (jpeg_io) free(jpeg_io);
                if (out_info) free(out_info);
            } else {
                ESP_LOGW(TAG, "JPEG decode task: invalid buffer or size=0");
            }
        } else {
            ESP_LOGD(TAG, "JPEG decode task: waiting for data...");
        }
    }

    // 清理解码器
    if (jpeg_dec) {
        jpeg_dec_close(jpeg_dec);
    }

    ESP_LOGI(TAG, "JPEG decode task stopped");
    s_jpeg_decode_task_handle = NULL;
    vTaskDelete(NULL);
}

void jpeg_decoder_service_process_data(const uint8_t* data, size_t length) {
    if (!s_jpeg_service_running || !s_data_callback) {
        return;
    }

    ESP_LOGI(TAG, "Processing JPEG data: %zu bytes", length);

    // 确保JPEG缓冲区足够大
    if (!s_jpeg_buffer || s_max_jpeg_size < length) {
        if (s_jpeg_buffer) {
            free(s_jpeg_buffer);
        }
        s_jpeg_buffer = heap_caps_aligned_alloc(16, length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_jpeg_buffer) {
            ESP_LOGE(TAG, "Failed to allocate JPEG buffer");
            return;
        }
        s_max_jpeg_size = length;
    }

    // 复制JPEG数据
    memcpy(s_jpeg_buffer, data, length);
    ESP_LOGI(TAG, "JPEG data copied to buffer");

    // 设置数据就绪标志，通知解码任务开始工作
    xEventGroupSetBits(s_jpeg_event_group, JPEG_DATA_READY_BIT);
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