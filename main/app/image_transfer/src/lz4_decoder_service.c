#include "lz4_decoder_service.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "lz4.h"
#include "lz4frame.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "LZ4_DECODER_SERVICE";

// 全局状态变量
static bool s_lz4_service_running = false;
static uint8_t* s_compressed_buffer = NULL;
static uint8_t* s_decompressed_buffer = NULL;
static size_t s_max_compressed_size = 0;
static size_t s_max_decompressed_size = 0;
static size_t s_actual_decompressed_size = 0;  // 实际解压缩数据大小
static EventGroupHandle_t s_lz4_event_group = NULL;
static lz4_decoder_callback_t s_data_callback = NULL;
static void* s_callback_context = NULL;

#define LZ4_DATA_READY_BIT (1 << 0)


void lz4_decoder_service_process_data(const uint8_t* data, size_t length) {
    if (!s_lz4_service_running || !s_data_callback) {
        ESP_LOGW(TAG, "LZ4 service not running or no callback: running=%d, callback=%p",
                s_lz4_service_running, s_data_callback);
        return;
    }

    ESP_LOGI(TAG, "LZ4 processing data: %zu bytes", length);
    
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
    
    // 估计解压缩后的大小（LZ4帧格式可能需要更大的缓冲区）
    // 对于典型的320x240 RGB565图像，原始大小约为150KB
    // LZ4压缩比通常为2:1到4:1，所以分配更大的缓冲区
    size_t min_buffer_size = 320 * 240 * 2; // 最小缓冲区：320x240 RGB565
    size_t estimated_decompressed_size = (length < min_buffer_size) ? min_buffer_size : length * 8;

    ESP_LOGI(TAG, "Estimated decompressed buffer size: %zu bytes (input: %zu)", estimated_decompressed_size, length);
    
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
    
    // 执行LZ4帧解压缩
    ESP_LOGI(TAG, "Starting LZ4 frame decompression, input size: %zu", length);

    // 检查LZ4帧格式的魔数 (0x184D2204 for LZ4 frame)
    if (length >= 4) {
        uint32_t magic = *(uint32_t*)s_compressed_buffer;
        ESP_LOGI(TAG, "LZ4 frame magic: 0x%08X", magic);
    }
    LZ4F_decompressionContext_t dctx = NULL;
    LZ4F_errorCode_t errorCode;

    // 创建解压缩上下文
    errorCode = LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION);
    if (LZ4F_isError(errorCode)) {
        ESP_LOGE(TAG, "LZ4F_createDecompressionContext failed: %s", LZ4F_getErrorName(errorCode));
        return;
    }
    ESP_LOGI(TAG, "LZ4 decompression context created successfully");

    // 设置输入和输出缓冲区
    LZ4F_decompressOptions_t options = {0};
    size_t srcSize = length;
    size_t dstSize = s_max_decompressed_size;
    size_t srcOffset = 0;
    size_t dstOffset = 0;

    // LZ4帧解压缩可能需要多次调用
    int max_iterations = 10; // 防止无限循环
    int iteration = 0;

    while (srcOffset < length && iteration < max_iterations) {
        size_t remainingSrc = length - srcOffset;
        size_t remainingDst = s_max_decompressed_size - dstOffset;

        ESP_LOGD(TAG, "LZ4 decompression iteration %d: src_offset=%zu/%zu, dst_offset=%zu/%zu",
                iteration, srcOffset, length, dstOffset, s_max_decompressed_size);

        if (remainingDst < 1024) { // 至少需要1KB的输出空间
            ESP_LOGE(TAG, "Output buffer nearly full during LZ4 decompression (remaining: %zu)", remainingDst);
            LZ4F_freeDecompressionContext(dctx);
            return;
        }

        size_t result = LZ4F_decompress(dctx,
                                       s_decompressed_buffer + dstOffset, &remainingDst,
                                       s_compressed_buffer + srcOffset, &remainingSrc,
                                       &options);

        if (LZ4F_isError(result)) {
            ESP_LOGE(TAG, "LZ4F_decompress failed: %s", LZ4F_getErrorName(result));
            LZ4F_freeDecompressionContext(dctx);
            return;
        }

        srcOffset += remainingSrc;
        dstOffset += remainingDst;
        iteration++;

        // 如果这是最后一帧，退出循环
        if (result == 0) {
            ESP_LOGI(TAG, "LZ4 decompression finished at iteration %d", iteration);
            break;
        }
    }

    if (iteration >= max_iterations) {
        ESP_LOGW(TAG, "LZ4 decompression reached maximum iterations (%d)", max_iterations);
    }

    int decompressed_size = (int)dstOffset;
    ESP_LOGI(TAG, "LZ4 frame decompression completed: %d bytes output (buffer size: %zu)", decompressed_size, s_max_decompressed_size);

    // 清理上下文
    LZ4F_freeDecompressionContext(dctx);
    
    if (decompressed_size > 0) {
        // 保存实际解压缩数据大小
        s_actual_decompressed_size = decompressed_size;

        // 设置数据就绪标志
        xEventGroupSetBits(s_lz4_event_group, LZ4_DATA_READY_BIT);

        ESP_LOGI(TAG, "LZ4 decompression successful: %d -> %zu bytes", length, decompressed_size);

        // 调用数据回调
        s_data_callback(s_decompressed_buffer, decompressed_size, s_callback_context);
    } else {
        ESP_LOGE(TAG, "LZ4 decompression failed: %d", decompressed_size);
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
    s_actual_decompressed_size = 0;  // 重置实际解压缩大小
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
        s_actual_decompressed_size = 0;
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
    if (!s_lz4_service_running || !s_decompressed_buffer || s_actual_decompressed_size == 0) {
        return false;
    }

    *frame_buffer = s_decompressed_buffer;
    *frame_size = s_actual_decompressed_size;
    ESP_LOGD(TAG, "LZ4 get_latest_frame: returning %zu bytes", s_actual_decompressed_size);
    return true;
}

// 帧数据解锁（允许新的数据写入）
void lz4_decoder_service_frame_unlock(void) {
    if (s_lz4_event_group) {
        xEventGroupClearBits(s_lz4_event_group, LZ4_DATA_READY_BIT);
    }
}