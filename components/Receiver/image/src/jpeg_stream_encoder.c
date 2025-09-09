#include "jpeg_stream_encoder.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "settings_manager.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "jpeg_encoder";

// JPEG编码器全局变量
static jpeg_enc_handle_t s_jpeg_enc = NULL;
static QueueHandle_t s_jpeg_queue = NULL;
static TaskHandle_t s_jpeg_task = NULL;
static jpeg_output_callback_t s_output_callback = NULL;

// JPEG编码缓冲区
static uint8_t* s_jpeg_input_buffer = NULL;
static uint8_t* s_jpeg_output_buffer = NULL;
static size_t s_jpeg_input_buffer_size = 0;
static size_t s_jpeg_output_buffer_size = 0;
static size_t s_jpeg_data_len = 0;
static uint8_t s_jpeg_quality = JPEG_ENC_QUALITY;

// 前向声明
static void jpeg_encode_feed_task(void* arg);
static esp_err_t init_jpeg_encoder_internal(void);
static void cleanup_jpeg_encoder_internal(void);
static void on_jpeg_quality_changed(setting_type_t type, const setting_value_t* new_value);

// JPEG编码任务实现
static void jpeg_encode_feed_task(void* arg) {
    ESP_LOGI(TAG, "JPEG feed task started");
    jpeg_chunk_msg_t msg;
    
    while (1) {
        if (xQueueReceive(s_jpeg_queue, &msg, portMAX_DELAY) == pdTRUE) {
            if (msg.data == NULL && msg.len == 0) {
                // 退出信号
                break;
            }
            
            if (msg.data && msg.len > 0 && s_jpeg_enc) {
                // 累积数据到输入缓冲区
                if (s_jpeg_data_len + msg.len <= s_jpeg_input_buffer_size) {
                    memcpy(s_jpeg_input_buffer + s_jpeg_data_len, msg.data, msg.len);
                    s_jpeg_data_len += msg.len;
                    
                    // 检查是否收集到足够的数据进行编码
                    size_t expected_size = JPEG_ENC_WIDTH * JPEG_ENC_HEIGHT * 4; // RGBA格式
                    if (s_jpeg_data_len >= expected_size) {
                        // 执行JPEG编码
                        int out_len = 0;
                        jpeg_error_t ret = jpeg_enc_process(s_jpeg_enc, s_jpeg_input_buffer, 
                                                          expected_size, s_jpeg_output_buffer, 
                                                          s_jpeg_output_buffer_size, &out_len);
                        if (ret == JPEG_ERR_OK && out_len > 0) {
                            ESP_LOGI(TAG, "JPEG encoded: %d bytes -> %d bytes", expected_size, out_len);
                            // 调用回调函数处理编码后的数据
                            if (s_output_callback) {
                                s_output_callback(s_jpeg_output_buffer, out_len);
                            }
                        } else {
                            ESP_LOGW(TAG, "JPEG encode failed: %d", ret);
                        }
                        s_jpeg_data_len = 0; // 重置缓冲区
                    }
                } else {
                    ESP_LOGW(TAG, "Input buffer overflow, dropping data");
                    s_jpeg_data_len = 0; // 重置缓冲区
                }
            }
            
            if (msg.data) free(msg.data);
        }
    }
    ESP_LOGI(TAG, "JPEG feed task stopped");
    vTaskDelete(NULL);
}

// 内部初始化函数
static esp_err_t init_jpeg_encoder_internal(void) {
    // 分配缓冲区
    s_jpeg_input_buffer_size = JPEG_ENC_WIDTH * JPEG_ENC_HEIGHT * 4; // RGBA
    s_jpeg_output_buffer_size = 100 * 1024; // 100KB

    ESP_LOGI(TAG, "Available SPIRAM before malloc: %u bytes", (unsigned long)esp_get_free_heap_size());
    ESP_LOGI(TAG, "Available internal memory: %u bytes", (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "Available SPIRAM memory: %u bytes", (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    
    // 添加更长延迟确保SPIRAM完全初始化，并尝试小块分配测试
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 先尝试小块SPIRAM分配测试
    void* test_ptr = heap_caps_malloc(1024, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (test_ptr == NULL) {
        ESP_LOGE(TAG, "SPIRAM test allocation failed! SPIRAM may not be ready.");
        return ESP_ERR_NO_MEM;
    }
    heap_caps_free(test_ptr);
    ESP_LOGI(TAG, "SPIRAM test allocation successful");
    
    ESP_LOGI(TAG, "Attempting to allocate %d bytes for input buffer from SPIRAM...", s_jpeg_input_buffer_size);
    
    // 分配输入缓冲区 - 强制使用SPIRAM
    s_jpeg_input_buffer = (uint8_t*)heap_caps_malloc(s_jpeg_input_buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_jpeg_input_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate input buffer from SPIRAM!");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "JPEG encoder input buffer allocated from SPIRAM: %d bytes", s_jpeg_input_buffer_size);

    ESP_LOGI(TAG, "Attempting to allocate %d bytes for output buffer from SPIRAM...", s_jpeg_output_buffer_size);
    
    // 分配输出缓冲区 - 强制使用SPIRAM
    s_jpeg_output_buffer = (uint8_t*)heap_caps_malloc(s_jpeg_output_buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_jpeg_output_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate output buffer from SPIRAM!");
        heap_caps_free(s_jpeg_input_buffer);
        s_jpeg_input_buffer = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "JPEG encoder output buffer allocated from SPIRAM: %d bytes", s_jpeg_output_buffer_size);

    // 创建标准ESP-IDF JPEG编码器
    jpeg_enc_config_t jpeg_cfg = DEFAULT_JPEG_ENC_CONFIG();
    jpeg_cfg.width = JPEG_ENC_WIDTH;
    jpeg_cfg.height = JPEG_ENC_HEIGHT;
    jpeg_cfg.src_type = JPEG_ENC_SRC_TYPE;
    jpeg_cfg.subsampling = JPEG_ENC_SUBSAMPLE;
    jpeg_cfg.quality = s_jpeg_quality;
    jpeg_cfg.rotate = JPEG_ROTATE_0D;
    jpeg_cfg.task_enable = true;
    jpeg_cfg.hfm_task_core = 1;
    jpeg_cfg.hfm_task_priority = 10;

    ESP_LOGI(TAG, "JPEG encoder config: %d %d %d %d", jpeg_cfg.width, jpeg_cfg.height, jpeg_cfg.src_type, jpeg_cfg.quality);
    
    esp_err_t ret = jpeg_enc_open(&jpeg_cfg, &s_jpeg_enc);
    if (ret != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "JPEG encoder open failed: %d", ret);
        cleanup_jpeg_encoder_internal();
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "jpeg_enc_open success");
    
    // 缓冲区分配成功，初始化数据长度
    s_jpeg_data_len = 0;
    ESP_LOGI(TAG, "JPEG encoder initialized: %dx%d fmt=%d q=%d", 
             jpeg_cfg.width, jpeg_cfg.height, jpeg_cfg.src_type, jpeg_cfg.quality);
    
    return ESP_OK;
}

// 内部清理函数
static void cleanup_jpeg_encoder_internal(void) {
    // 清理JPEG编码器资源
    if (s_jpeg_enc) {
        jpeg_enc_close(s_jpeg_enc);
        s_jpeg_enc = NULL;
    }
    
    if (s_jpeg_input_buffer) {
        heap_caps_free(s_jpeg_input_buffer);
        s_jpeg_input_buffer = NULL;
    }
    
    if (s_jpeg_output_buffer) {
        heap_caps_free(s_jpeg_output_buffer);
        s_jpeg_output_buffer = NULL;
    }
    
    s_jpeg_input_buffer_size = 0;
    s_jpeg_output_buffer_size = 0;
    s_jpeg_data_len = 0;
}

// JPEG质量变化回调
static void on_jpeg_quality_changed(setting_type_t type, const setting_value_t* new_value) {
    if (type == SETTING_JPEG_QUALITY && new_value && new_value->uint8_value != s_jpeg_quality) {
        s_jpeg_quality = new_value->uint8_value;
        ESP_LOGI(TAG, "JPEG quality updated to: %d", s_jpeg_quality);
    }
}

// 公共API实现
esp_err_t jpeg_stream_encoder_init(jpeg_output_callback_t output_callback) {
    if (s_jpeg_enc != NULL) {
        ESP_LOGW(TAG, "JPEG encoder already initialized");
        return ESP_OK;
    }
    
    s_output_callback = output_callback;
    
    // 注册设置变化回调
    settings_register_callback(on_jpeg_quality_changed);
    
    // 从设置管理器获取当前JPEG质量
    setting_value_t quality_val;
    if (settings_get(SETTING_JPEG_QUALITY, &quality_val) == ESP_OK) {
        s_jpeg_quality = quality_val.uint8_value;
    }
    
    return init_jpeg_encoder_internal();
}

esp_err_t jpeg_stream_encoder_start(void) {
    if (s_jpeg_enc == NULL) {
        ESP_LOGE(TAG, "JPEG encoder not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_jpeg_queue != NULL || s_jpeg_task != NULL) {
        ESP_LOGW(TAG, "JPEG encoder already started");
        return ESP_OK;
    }
    
    // 创建编码消息队列
    s_jpeg_queue = xQueueCreate(16, sizeof(jpeg_chunk_msg_t));
    if (!s_jpeg_queue) {
        ESP_LOGE(TAG, "Failed to create JPEG queue");
        return ESP_ERR_NO_MEM;
    }
    
    // 创建编码任务
    if (xTaskCreatePinnedToCore(jpeg_encode_feed_task, "jpeg_feed", 8192, NULL, 9, &s_jpeg_task, tskNO_AFFINITY) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create JPEG feed task");
        vQueueDelete(s_jpeg_queue);
        s_jpeg_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "JPEG encoder started successfully");
    return ESP_OK;
}

void jpeg_stream_encoder_stop(void) {
    // 停止JPEG编码任务
    if (s_jpeg_task) {
        // 发送退出信号
        jpeg_chunk_msg_t quit = {.data = NULL, .len = 0};
        if (s_jpeg_queue) {
            xQueueSend(s_jpeg_queue, &quit, 0);
        }
        // 等待任务自行退出
        vTaskDelay(pdMS_TO_TICKS(100));
        s_jpeg_task = NULL;
    }
    
    // 清理队列
    if (s_jpeg_queue) {
        // 清空残留消息并释放内存
        jpeg_chunk_msg_t m;
        while (xQueueReceive(s_jpeg_queue, &m, 0) == pdTRUE) {
            if (m.data) free(m.data);
        }
        vQueueDelete(s_jpeg_queue);
        s_jpeg_queue = NULL;
    }
    
    // 清理编码器资源
    cleanup_jpeg_encoder_internal();
    
    // 注意：settings_manager没有提供注销回调的接口
    // 在实际应用中，可能需要在settings_manager中添加此功能
    
    s_output_callback = NULL;
    
    ESP_LOGI(TAG, "JPEG encoder stopped");
}

esp_err_t jpeg_stream_encoder_feed_data(const uint8_t* data, size_t len) {
    if (!s_jpeg_queue || !data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 分配内存并复制数据
    uint8_t* data_copy = malloc(len);
    if (!data_copy) {
        ESP_LOGE(TAG, "Failed to allocate memory for data copy");
        return ESP_ERR_NO_MEM;
    }
    
    memcpy(data_copy, data, len);
    
    jpeg_chunk_msg_t msg = {
        .data = data_copy,
        .len = len
    };
    
    if (xQueueSend(s_jpeg_queue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
        free(data_copy);
        ESP_LOGW(TAG, "Failed to send data to JPEG queue");
        return ESP_ERR_TIMEOUT;
    }
    
    return ESP_OK;
}

QueueHandle_t jpeg_stream_encoder_get_queue(void) {
    return s_jpeg_queue;
}

esp_err_t jpeg_stream_encoder_set_quality(uint8_t quality) {
    if (quality < 1 || quality > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_jpeg_quality = quality;
    
    // 如果编码器已初始化，需要重新配置
    if (s_jpeg_enc) {
        ESP_LOGI(TAG, "Updating JPEG quality to: %d", quality);
        // 注意：ESP-IDF的JPEG编码器可能需要重新创建才能更新质量
        // 这里简单更新变量，实际应用中可能需要重新初始化编码器
    }
    
    return ESP_OK;
}

uint8_t jpeg_stream_encoder_get_quality(void) {
    return s_jpeg_quality;
}