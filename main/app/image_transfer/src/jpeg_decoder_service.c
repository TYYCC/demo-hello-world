#include "jpeg_decoder_service.h"
#include "display_queue.h"
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
static uint8_t* s_ui_buffer_A = NULL;      // 三重缓冲 - 缓冲区A
static uint8_t* s_ui_buffer_B = NULL;      // 三重缓冲 - 缓冲区B
static uint8_t* s_active_ui_buffer = NULL; // 当前活跃的UI缓冲区指针
static size_t s_max_jpeg_size = 0;
static size_t s_max_decoded_size = 0;
static size_t s_ui_buffer_size = 0; // UI缓冲区大小
static int s_frame_width = 0;       // 当前帧宽度
static int s_frame_height = 0;      // 当前帧高度
static bool s_frame_ready = false;  // 新帧是否就绪
static EventGroupHandle_t s_jpeg_event_group = NULL;
static SemaphoreHandle_t s_buffer_mutex = NULL; // 缓冲区互斥锁
static jpeg_decoder_callback_t s_data_callback = NULL;
static void* s_callback_context = NULL;
static TaskHandle_t s_jpeg_decode_task_handle = NULL;
static volatile bool s_buffer_swap_pending = false; // 缓冲区交换挂起标志
static QueueHandle_t s_display_queue = NULL; // 显示队列句柄

#define JPEG_DATA_READY_BIT (1 << 0)
#define JPEG_BUFFER_LOCK_BIT (1 << 1) // 缓冲区锁定位

// JPEG解码任务函数
static void jpeg_decode_task(void* pvParameters) {
    // 初始化JPEG解码器
    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    // 提示词要求：最终显示缓冲为 RGB565LE
    config.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;

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
                                free(s_decoded_buffer);
                            }
                            s_decoded_buffer = heap_caps_aligned_alloc(
                                16, required_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                            if (!s_decoded_buffer) {
                                ESP_LOGE(TAG, "Failed to allocate decoded buffer");
                                free(jpeg_io);
                                free(out_info);
                                continue;
                            }
                            s_max_decoded_size = required_size;
                        }

                        // 确保UI缓冲区A和B都足够大（三重缓冲）
                        if (!s_ui_buffer_A || s_ui_buffer_size < required_size) {
                            // 获取互斥锁以确保UI不在使用缓冲区
                            if (xSemaphoreTake(s_buffer_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
                                ESP_LOGW(TAG, "Failed to take buffer mutex, skipping frame");
                                free(jpeg_io);
                                free(out_info);
                                continue;
                            }

                            // 释放旧缓冲区（如果存在）
                            if (s_ui_buffer_A) {
                                free(s_ui_buffer_A);
                            }
                            if (s_ui_buffer_B) {
                                free(s_ui_buffer_B);
                            }

                            // 分配新的三重缓冲区
                            s_ui_buffer_A = heap_caps_aligned_alloc(
                                16, required_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                            s_ui_buffer_B = heap_caps_aligned_alloc(
                                16, required_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

                            if (!s_ui_buffer_A || !s_ui_buffer_B) {
                                ESP_LOGE(TAG, "Failed to allocate triple buffer");
                                if (s_ui_buffer_A) {
                                    free(s_ui_buffer_A);
                                    s_ui_buffer_A = NULL;
                                }
                                if (s_ui_buffer_B) {
                                    free(s_ui_buffer_B);
                                    s_ui_buffer_B = NULL;
                                }
                                xSemaphoreGive(s_buffer_mutex);
                                free(jpeg_io);
                                free(out_info);
                                continue;
                            }

                            s_ui_buffer_size = required_size;
                            s_active_ui_buffer = s_ui_buffer_A; // 初始化活跃缓冲区
                            xSemaphoreGive(s_buffer_mutex);
                        }

                        // 设置输出缓冲区为解码缓冲区
                        jpeg_io->outbuf = s_decoded_buffer;

                        // 执行JPEG解码
                        ESP_LOGD(TAG, "JPEG decode task: processing decode...");
                        dec_ret = jpeg_dec_process(jpeg_dec, jpeg_io);
                        if (dec_ret == JPEG_ERR_OK) {
                            ESP_LOGD(TAG, "JPEG decoded: %dx%d", out_info->width, out_info->height);

                            // 获取互斥锁，确保UI不在读取缓冲区
                            if (xSemaphoreTake(s_buffer_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                                // 检查是否有缓冲区交换正在挂起，如果挂起时间过长则自动清除
                                if (s_buffer_swap_pending) {
                                    static TickType_t last_pending_time = 0;
                                    TickType_t current_time = xTaskGetTickCount();
                                    
                                    // 如果挂起时间超过500ms，自动清除挂起状态
                                    if (last_pending_time == 0) {
                                        last_pending_time = current_time;
                                    }
                                    
                                    if ((current_time - last_pending_time) > pdMS_TO_TICKS(500)) {
                                        ESP_LOGW(TAG, "Buffer swap pending for too long (%lu ms), auto-clearing", 
                                               pdTICKS_TO_MS(current_time - last_pending_time));
                                        s_buffer_swap_pending = false;
                                        last_pending_time = 0;
                                    } else {
                                        ESP_LOGW(TAG, "Buffer swap pending, skipping frame");
                                        xSemaphoreGive(s_buffer_mutex);
                                        continue;
                                    }
                                }

                                // 记录当前帧尺寸
                                s_frame_width = out_info->width;
                                s_frame_height = out_info->height;

                                // 确定要写入的非活跃缓冲区
                                uint8_t* target_buffer = (s_active_ui_buffer == s_ui_buffer_A)
                                                             ? s_ui_buffer_B
                                                             : s_ui_buffer_A;

                                // 将解码数据复制到非活跃的UI缓冲区
                                memcpy(target_buffer, s_decoded_buffer, required_size);

                                // 分配新的缓冲区用于显示队列（避免三重缓冲区被错误释放）
                                uint8_t* display_buffer = heap_caps_aligned_alloc(
                                    16, required_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                                if (display_buffer) {
                                    // 将数据复制到新分配的缓冲区
                                    memcpy(display_buffer, target_buffer, required_size);
                                    
                                    // 推送到 DisplayQueue（RGB565LE）
                                    frame_msg_t msg = {.magic = FRAME_MSG_MAGIC,
                                                       .type = FRAME_TYPE_JPEG,
                                                       .width = (uint16_t)s_frame_width,
                                                       .height = (uint16_t)s_frame_height,
                                                       .payload_len = (uint32_t)required_size,
                                                       .frame_buffer = display_buffer};
                                    if (!display_queue_enqueue(s_display_queue, &msg)) {
                                        ESP_LOGW(TAG, "Display queue full, dropping oldest frame");
                                        // 如果入队失败，释放分配的缓冲区
                                        heap_caps_free(display_buffer);
                                    }
                                } else {
                                    ESP_LOGE(TAG, "Failed to allocate display buffer");
                                }

                                // 标记缓冲区交换挂起（供 UI 接口使用仍保留）
                                s_buffer_swap_pending = true;
                                s_frame_ready = true;

                                // 释放互斥锁
                                xSemaphoreGive(s_buffer_mutex);
                            } else {
                                ESP_LOGW(TAG,
                                         "JPEG decode task: buffer mutex timeout, frame skipped");
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

void jpeg_decoder_service_process_data(const uint8_t* data, size_t length) {
    if (!s_jpeg_service_running || !s_data_callback) {
        return;
    }

    ESP_LOGD(TAG, "Processing JPEG data: %zu bytes", length);

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

    // 创建互斥锁
    s_buffer_mutex = xSemaphoreCreateMutex();
    if (!s_buffer_mutex) {
        ESP_LOGE(TAG, "Failed to create buffer mutex");
        vEventGroupDelete(s_jpeg_event_group);
        s_jpeg_event_group = NULL;
        return ESP_FAIL;
    }

    s_data_callback = data_callback;
    s_callback_context = context;
    s_display_queue = display_queue;
    s_jpeg_service_running = true;

    // 初始化帧尺寸和缓冲区状态
    s_frame_width = 0;
    s_frame_height = 0;
    s_frame_ready = false;
    s_buffer_swap_pending = false;
    s_active_ui_buffer = NULL;

    // 创建JPEG解码任务
    BaseType_t result = xTaskCreate(jpeg_decode_task, "jpeg_decode",
                                    4096, // 堆栈大小
                                    NULL,
                                    5, // 优先级
                                    &s_jpeg_decode_task_handle);

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create JPEG decode task");
        vSemaphoreDelete(s_buffer_mutex);
        s_buffer_mutex = NULL;
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

    // 释放三重缓冲区
    if (s_buffer_mutex) {
        if (xSemaphoreTake(s_buffer_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (s_ui_buffer_A) {
                free(s_ui_buffer_A);
                s_ui_buffer_A = NULL;
            }

            if (s_ui_buffer_B) {
                free(s_ui_buffer_B);
                s_ui_buffer_B = NULL;
            }

            s_active_ui_buffer = NULL;
            s_ui_buffer_size = 0;
            xSemaphoreGive(s_buffer_mutex);
        } else {
            // 如果无法获取锁，仍然需要释放内存
            if (s_ui_buffer_A) {
                free(s_ui_buffer_A);
                s_ui_buffer_A = NULL;
            }

            if (s_ui_buffer_B) {
                free(s_ui_buffer_B);
                s_ui_buffer_B = NULL;
            }

            s_active_ui_buffer = NULL;
            s_ui_buffer_size = 0;
        }
    }

    // 删除互斥锁
    if (s_buffer_mutex) {
        vSemaphoreDelete(s_buffer_mutex);
        s_buffer_mutex = NULL;
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
    s_frame_ready = false;
    s_buffer_swap_pending = false;

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

// 获取解码后的帧数据（UI线程使用）
bool jpeg_decoder_service_get_frame_data(uint8_t** data, int* width, int* height) {
    if (!s_jpeg_service_running || !s_buffer_mutex || !s_active_ui_buffer) {
        return false;
    }

    bool result = false;

    // 获取互斥锁以安全访问缓冲区
    if (xSemaphoreTake(s_buffer_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        // 检查是否有新帧就绪，如果有就执行缓冲区交换
        if (s_buffer_swap_pending && s_frame_ready) {
            // 交换缓冲区指针
            s_active_ui_buffer =
                (s_active_ui_buffer == s_ui_buffer_A) ? s_ui_buffer_B : s_ui_buffer_A;
            s_buffer_swap_pending = false; // 清除交换挂起标志

            ESP_LOGD(TAG, "Buffer swapped, active buffer is now %s",
                     (s_active_ui_buffer == s_ui_buffer_A) ? "A" : "B");
        }

        // 返回当前活跃缓冲区的数据
        if (s_active_ui_buffer && s_frame_width > 0 && s_frame_height > 0) {
            *data = s_active_ui_buffer;
            *width = s_frame_width;
            *height = s_frame_height;
            result = true;
        }

        xSemaphoreGive(s_buffer_mutex);
    } else {
        ESP_LOGW(TAG, "Failed to take buffer mutex for frame data access");
    }

    return result;
}