#include "jpeg_decoder_service.h"
#include "display_queue.h"
#include "esp_jpeg_common.h"
#include "esp_jpeg_dec.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char* TAG = "JPEG_DECODER_SERVICE";

// 全局状态变量
static bool s_jpeg_service_running = false;
static uint8_t* s_jpeg_buffer = NULL;
static uint8_t* s_decoded_buffer = NULL;
// 三重缓冲系统已移除，现在直接使用显示队列
static size_t s_max_jpeg_size = 0;
static size_t s_max_decoded_size = 0;
static int s_frame_width = 0;       // 当前帧宽度
static int s_frame_height = 0;      // 当前帧高度
static EventGroupHandle_t s_jpeg_event_group = NULL;
static jpeg_decoder_callback_t s_data_callback = NULL;
static void* s_callback_context = NULL;
static TaskHandle_t s_jpeg_decode_task_handle = NULL;
static QueueHandle_t s_display_queue = NULL; // 显示队列句柄

#define JPEG_DATA_READY_BIT (1 << 0)
#define JPEG_BUFFER_LOCK_BIT (1 << 1) // 缓冲区锁定位

// JPEG解码任务函数
static void jpeg_decode_task(void* pvParameters) {
    // 初始化JPEG解码器
    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    // LVGL配置了LV_COLOR_16_SWAP=1，需要使用BE格式以匹配字节序
    config.output_type = JPEG_PIXEL_FORMAT_RGB565_BE;

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
        EventBits_t bits = xEventGroupWaitBits(s_jpeg_event_group, JPEG_DATA_READY_BIT,
                                               pdTRUE,              // 清除标志
                                               pdFALSE,             // 等待所有位
                                               pdMS_TO_TICKS(100)); // 100ms超时，避免永久阻塞

        if (bits & JPEG_DATA_READY_BIT) {
            ESP_LOGD(TAG, "JPEG decode task: received data ready signal");
            if (s_jpeg_buffer && s_max_jpeg_size > 0) {
                ESP_LOGD(TAG, "JPEG decode task: buffer valid, size=%zu", s_max_jpeg_size);
                // 解析JPEG头部信息
                jpeg_dec_io_t* jpeg_io = calloc(1, sizeof(jpeg_dec_io_t));
                jpeg_dec_header_info_t* out_info = calloc(1, sizeof(jpeg_dec_header_info_t));

                if (jpeg_io && out_info) {
                    // 设置输入缓冲区
                    jpeg_io->inbuf = s_jpeg_buffer;
                    jpeg_io->inbuf_len = s_max_jpeg_size;

                    // 解析JPEG头部
                    ESP_LOGD(TAG, "JPEG decode task: parsing header...");
                    dec_ret = jpeg_dec_parse_header(jpeg_dec, jpeg_io, out_info);
                    if (dec_ret == JPEG_ERR_OK) {
                        ESP_LOGD(TAG, "JPEG header parsed: %dx%d", out_info->width,
                                 out_info->height);

                        // 确保解码缓冲区足够大
                        size_t required_size =
                            out_info->width * out_info->height * 2; // RGB565 = 2字节/像素
                        if (!s_decoded_buffer || s_max_decoded_size < required_size) {
                            if (s_decoded_buffer) {
                                jpeg_free_align(s_decoded_buffer);
                            }
                            s_decoded_buffer = jpeg_calloc_align(required_size, 16);
                            if (!s_decoded_buffer) {
                                ESP_LOGE(TAG, "Failed to allocate decoded buffer");
                                free(jpeg_io);
                                free(out_info);
                                continue;
                            }
                            s_max_decoded_size = required_size;
                        }

                        // UI缓冲区管理已简化，直接使用显示队列

                        // 设置输出缓冲区为解码缓冲区
                        jpeg_io->outbuf = s_decoded_buffer;

                        // 执行JPEG解码
                        ESP_LOGD(TAG, "JPEG decode task: processing decode...");
                        dec_ret = jpeg_dec_process(jpeg_dec, jpeg_io);
                        if (dec_ret == JPEG_ERR_OK) {
                            ESP_LOGD(TAG, "JPEG decoded: %dx%d", out_info->width, out_info->height);

                            // 记录当前帧尺寸
                            s_frame_width = out_info->width;
                            s_frame_height = out_info->height;

                            // 直接使用解码缓冲区推送到显示队列，避免额外的内存复制
                            // 推送到 DisplayQueue（RGB565LE）
                            frame_msg_t msg = {.magic = FRAME_MSG_MAGIC,
                                               .type = FRAME_TYPE_JPEG,
                                               .width = (uint16_t)s_frame_width,
                                               .height = (uint16_t)s_frame_height,
                                               .payload_len = (uint32_t)required_size,
                                               .frame_buffer = s_decoded_buffer};
                            
                            if (display_queue_enqueue(s_display_queue, &msg)) {
                                // 入队成功，将解码缓冲区所有权转移给显示队列
                                // 分配新的解码缓冲区供下一帧使用
                                s_decoded_buffer = jpeg_calloc_align(required_size, 16);
                                if (!s_decoded_buffer) {
                                    ESP_LOGE(TAG, "Failed to allocate new decoded buffer");
                                    s_max_decoded_size = 0;
                                } else {
                                    s_max_decoded_size = required_size;
                                }
                            } else {
                                ESP_LOGW(TAG, "Display queue full, dropping frame");
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
                if (jpeg_io)
                    free(jpeg_io);
                if (out_info)
                    free(out_info);
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

void jpeg_decoder_service_process_data(const uint8_t* data, size_t length, uint16_t width, uint16_t height) {
    if (!s_jpeg_service_running || !s_data_callback) {
        return;
    }

    ESP_LOGD(TAG, "Processing JPEG data: %zu bytes, expected size: %ux%u", length, width, height);

    // 验证JPEG数据完整性
    if (length < 4 || data[0] != 0xFF || data[1] != 0xD8) {
        ESP_LOGE(TAG, "Invalid JPEG data: missing SOI marker");
        return;
    }
    
    // 检查JPEG结束标记
    if (data[length-2] != 0xFF || data[length-1] != 0xD9) {
        ESP_LOGW(TAG, "JPEG data may be incomplete: missing EOI marker");
    }

    // 确保JPEG缓冲区足够大
    if (!s_jpeg_buffer || s_max_jpeg_size < length) {
        if (s_jpeg_buffer) {
            jpeg_free_align(s_jpeg_buffer);
        }
        s_jpeg_buffer = jpeg_calloc_align(length, 16);
        if (!s_jpeg_buffer) {
            ESP_LOGE(TAG, "Failed to allocate JPEG buffer");
            return;
        }
        s_max_jpeg_size = length;
    }

    // 复制JPEG数据
    memcpy(s_jpeg_buffer, data, length);
    ESP_LOGD(TAG, "JPEG data copied to buffer");

    // 设置数据就绪标志，通知解码任务开始工作
    xEventGroupSetBits(s_jpeg_event_group, JPEG_DATA_READY_BIT);
}

// 初始化JPEG解码服务
esp_err_t jpeg_decoder_service_init(jpeg_decoder_callback_t data_callback, void* context, QueueHandle_t display_queue) {
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

    // 互斥锁已移除，使用更简单的缓冲区管理

    s_data_callback = data_callback;
    s_callback_context = context;
    s_display_queue = display_queue;
    s_jpeg_service_running = true;

    // 初始化帧尺寸
    s_frame_width = 0;
    s_frame_height = 0;

    // 创建JPEG解码任务
    BaseType_t result = xTaskCreate(jpeg_decode_task, "jpeg_decode",
                                    4096, // 堆栈大小
                                    NULL,
                                    5, // 优先级
                                    &s_jpeg_decode_task_handle);

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
        jpeg_free_align(s_jpeg_buffer);
        s_jpeg_buffer = NULL;
        s_max_jpeg_size = 0;
    }

    if (s_decoded_buffer) {
        jpeg_free_align(s_decoded_buffer);
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
    s_frame_width = 0;
    s_frame_height = 0;

    ESP_LOGI(TAG, "JPEG decoder service stopped");
}

// 获取JPEG解码服务状态
bool jpeg_decoder_service_is_running(void) { return s_jpeg_service_running; }

// 获取事件组句柄
EventGroupHandle_t jpeg_decoder_service_get_event_group(void) { return s_jpeg_event_group; }

// 帧数据解锁（允许新的数据写入）
void jpeg_decoder_service_frame_unlock(void) {
    if (s_jpeg_event_group) {
        xEventGroupClearBits(s_jpeg_event_group, JPEG_DATA_READY_BIT);
    }
}

// 旧的帧数据获取函数已移除，现在使用显示队列系统