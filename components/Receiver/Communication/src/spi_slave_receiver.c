#include "spi_slave_receiver.h"
#include "driver/gpio.h"
#include "driver/spi_slave.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "jpeg_stream_encoder.h"
#include "settings_manager.h"
#include "tcp_common_protocol.h"
#include "cmd_terminal.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char* TAG = "spi_rx";

// 接收缓冲与解析缓冲
static uint8_t* s_rx_dma_bufs[SPI_RX_QUEUE_SIZE];
static uint8_t* s_parse_buf = NULL;
static size_t s_parse_len = 0;
static TaskHandle_t s_task = NULL;
static SemaphoreHandle_t s_spi_trans_done_sem = NULL;
static spi_slave_transaction_t s_trans[SPI_RX_QUEUE_SIZE];

// JPEG输出回调函数
static void jpeg_output_callback(const uint8_t* data, size_t len) {
    // 这里可以将编码后的JPEG数据发送到其他地方
    ESP_LOGI(TAG, "JPEG output received: %d bytes", len);
    // TODO: 实现具体的输出处理逻辑
}

// 当SPI传输完成时，在中断上下文中调用此回调
static void IRAM_ATTR spi_post_trans_callback(spi_slave_transaction_t* trans) {
    BaseType_t task_woken = pdFALSE;
    if (s_spi_trans_done_sem) {
        xSemaphoreGiveFromISR(s_spi_trans_done_sem, &task_woken);
    }
    if (task_woken) {
        portYIELD_FROM_ISR();
    }
}

// 解析并回调
static void spi_parse_and_dispatch(const uint8_t* data, size_t len) {
    if (!data || !s_parse_buf || len < MIN_FRAME_SIZE)
        return;

    size_t pos = 0;
    while (pos + MIN_FRAME_SIZE <= len) {
        if (pos + sizeof(protocol_header_t) > len)
            break;
        
        // 检查帧头
        if (data[pos] != FRAME_HEADER_1 || data[pos + 1] != FRAME_HEADER_2) {
            pos++;
            continue;
        }

        // 获取协议头信息
        protocol_header_t* header = (protocol_header_t*)&data[pos];
        size_t frame_size = sizeof(protocol_header_t) + header->length + sizeof(uint16_t);
        
        if (pos + frame_size > len)
            break;

        // 验证帧
        if (validate_frame(&data[pos], frame_size)) {
            switch (header->frame_type) {
            case FRAME_TYPE_COMMAND:
                // 处理命令帧（如遥控数据）
                ESP_LOGI(TAG, "Received command frame");
                break;
            case FRAME_TYPE_HEARTBEAT:
                // 处理心跳帧
                ESP_LOGI(TAG, "Received heartbeat frame");
                break;
            case FRAME_TYPE_EXTENDED:
                // 处理扩展帧
                ESP_LOGI(TAG, "Received extended frame");
                if (header->length >= sizeof(extended_cmd_payload_t)) {
                    handle_extended_command((const extended_cmd_payload_t*)&data[pos + sizeof(protocol_header_t)]);
                }
                break;
            default:
                ESP_LOGW(TAG, "Unknown frame type: 0x%02X", header->frame_type);
                break;
            }
        } else {
            ESP_LOGW(TAG, "Frame validation failed");
        }
        pos += frame_size;
    }

    if (pos < len) {
        size_t remain = len - pos;
        if (remain > SPI_RX_BUFFER_SZ)
            remain = SPI_RX_BUFFER_SZ; // clamp
        memmove(s_parse_buf, &data[pos], remain);
        s_parse_len = remain;
    } else {
        s_parse_len = 0;
    }
}

// SPI接收任务，现在由事件驱动
static void spi_rx_task(void* arg) {
    ESP_LOGI(TAG, "SPI 从机接收任务启动 (事件驱动)");

    if (!s_parse_buf) {
        ESP_LOGE(TAG, "Parse buffer not initialized");
        vTaskDelete(NULL);
        return;
    }

    // 延迟一小段时间，确保其他初始化可以继续进行，避免潜在的启动死锁
    vTaskDelay(pdMS_TO_TICKS(10));

    // 预先将所有事务排入队列
    for (int i = 0; i < SPI_RX_QUEUE_SIZE; i++) {
        memset(&s_trans[i], 0, sizeof(spi_slave_transaction_t));
        s_trans[i].length = SPI_RX_TRANSACTION_SZ * 8;
        s_trans[i].rx_buffer = s_rx_dma_bufs[i];

        esp_err_t ret = spi_slave_queue_trans(SPI_RX_HOST, &s_trans[i], portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Initial queue_trans err for trans #%d: %s", i, esp_err_to_name(ret));
        }
    }

    ESP_LOGI(TAG, "SPI transactions queued, waiting for incoming data...");

    while (1) {
        // 等待来自ISR的回调信号，表示一个事务已完成
        if (xSemaphoreTake(s_spi_trans_done_sem, portMAX_DELAY) == pdTRUE) {
            spi_slave_transaction_t* ret_trans = NULL;
            // 获取已完成的事务，超时设为0，因为它应该已经准备好了
            esp_err_t ret = spi_slave_get_trans_result(SPI_RX_HOST, &ret_trans, 0);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "get_trans_result err: %s", esp_err_to_name(ret));
                continue;
            }

            size_t bytes = ret_trans->trans_len / 8;
            if (bytes > 0) {
                uint8_t* rxp = (uint8_t*)ret_trans->rx_buffer;

                if (s_parse_len + bytes > SPI_RX_BUFFER_SZ) {
                    size_t to_copy = SPI_RX_BUFFER_SZ;
                    if (bytes < SPI_RX_BUFFER_SZ) {
                        memmove(s_parse_buf, &s_parse_buf[s_parse_len + bytes - SPI_RX_BUFFER_SZ],
                                SPI_RX_BUFFER_SZ - bytes);
                        memcpy(&s_parse_buf[SPI_RX_BUFFER_SZ - bytes], rxp, bytes);
                    } else {
                        memcpy(s_parse_buf, &rxp[bytes - SPI_RX_BUFFER_SZ], SPI_RX_BUFFER_SZ);
                    }
                    s_parse_len = to_copy;
                } else {
                    memcpy(&s_parse_buf[s_parse_len], rxp, bytes);
                    s_parse_len += bytes;
                }

                spi_parse_and_dispatch(s_parse_buf, s_parse_len);
                // 异步通过JPEG编码器处理数据，避免在SPI线程内执行编码
                if (bytes > 0) {
                    esp_err_t ret_jpeg = jpeg_stream_encoder_feed_data(ret_trans->rx_buffer, bytes);
                    if (ret_jpeg != ESP_OK) {
                        ESP_LOGW(TAG, "JPEG encoder feed failed: %s, drop %d bytes",
                                 esp_err_to_name(ret_jpeg), bytes);
                    }
                }
            }
            // 将刚刚处理完的事务重新排入队列，以备下次使用
            ret = spi_slave_queue_trans(SPI_RX_HOST, ret_trans, portMAX_DELAY);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "re-queue err: %s", esp_err_to_name(ret));
            }
        }
    }
}

esp_err_t spi_receiver_init(void) {
    // 创建用于任务同步的二进制信号量
    s_spi_trans_done_sem = xSemaphoreCreateBinary();
    if (!s_spi_trans_done_sem) {
        ESP_LOGE(TAG, "Failed to create semaphore");
        return ESP_ERR_NO_MEM;
    }

    // Allocate parse buffer from PSRAM to save internal RAM
    s_parse_buf = (uint8_t*)heap_caps_malloc(SPI_RX_BUFFER_SZ, MALLOC_CAP_SPIRAM);
    if (!s_parse_buf) {
        ESP_LOGE(TAG, "Failed to allocate parse buffer from PSRAM");
        vSemaphoreDelete(s_spi_trans_done_sem);
        s_spi_trans_done_sem = NULL;
        return ESP_ERR_NO_MEM;
    }
    // 初始化JPEG编码器
    if (jpeg_stream_encoder_init(jpeg_output_callback) != ESP_OK) {
        ESP_LOGW(TAG, "JPEG encoder initialization failed");
    } else {
        // 启动JPEG编码器
        if (jpeg_stream_encoder_start() != ESP_OK) {
            ESP_LOGE(TAG, "JPEG encoder start failed");
        }
    }

    gpio_set_pull_mode(SPI_SLAVE_PIN_CS, GPIO_PULLUP_ONLY);

    spi_bus_config_t bus_cfg = {.mosi_io_num = SPI_SLAVE_PIN_MOSI,
                                .miso_io_num = SPI_SLAVE_PIN_MISO,
                                .sclk_io_num = SPI_SLAVE_PIN_SCLK,
                                .quadwp_io_num = -1,
                                .quadhd_io_num = -1,
                                .max_transfer_sz = SPI_RX_TRANSACTION_SZ,
                                .flags = SPICOMMON_BUSFLAG_GPIO_PINS};

    spi_slave_interface_config_t slv_cfg = {.mode = 0,
                                            .spics_io_num = SPI_SLAVE_PIN_CS,
                                            .queue_size = SPI_RX_QUEUE_SIZE,
                                            .flags = 0,
                                            .post_trans_cb = spi_post_trans_callback};

    // 分配 DMA 缓冲（队列中每个事务一个）
    for (int i = 0; i < SPI_RX_QUEUE_SIZE; i++) {
        s_rx_dma_bufs[i] = (uint8_t*)heap_caps_malloc(SPI_RX_TRANSACTION_SZ, MALLOC_CAP_DMA);
        if (!s_rx_dma_bufs[i]) {
            ESP_LOGE(TAG, "DMA buffer alloc failed @%d", i);
            for (int j = 0; j < i; j++) {
                free(s_rx_dma_bufs[j]);
            }
            free(s_parse_buf);
            s_parse_buf = NULL;
            vSemaphoreDelete(s_spi_trans_done_sem);
            s_spi_trans_done_sem = NULL;
            return ESP_ERR_NO_MEM;
        }
    }

    // 使用 DMA 自动分配，提升吞吐
    esp_err_t ret = spi_slave_initialize(SPI_RX_HOST, &bus_cfg, &slv_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_slave_initialize fail: %s", esp_err_to_name(ret));
        for (int i = 0; i < SPI_RX_QUEUE_SIZE; i++) {
            if (s_rx_dma_bufs[i]) {
                free(s_rx_dma_bufs[i]);
            }
        }
        free(s_parse_buf);
        s_parse_buf = NULL;
        vSemaphoreDelete(s_spi_trans_done_sem);
        s_spi_trans_done_sem = NULL;
        return ret;
    }
    ESP_LOGI(TAG, "SPI 从机初始化完成: host=%d, MOSI=%d MISO=%d SCLK=%d CS=%d", SPI_RX_HOST,
             SPI_SLAVE_PIN_MOSI, SPI_SLAVE_PIN_MISO, SPI_SLAVE_PIN_SCLK, SPI_SLAVE_PIN_CS);

    return ESP_OK;
}

void spi_receiver_start(void) {
    if (s_task)
        return;
    xTaskCreatePinnedToCore(spi_rx_task, "spi_rx", 4096, NULL, 5, &s_task, 1);
}

void spi_receiver_stop(void) {
    if (s_task) {
        vTaskDelete(s_task);
        s_task = NULL;
    }
    spi_slave_free(SPI_RX_HOST);

    // 停止JPEG编码器
    jpeg_stream_encoder_stop();

    // 释放信号量
    if (s_spi_trans_done_sem) {
        vSemaphoreDelete(s_spi_trans_done_sem);
        s_spi_trans_done_sem = NULL;
    }

    // Free DMA buffers
    for (int i = 0; i < SPI_RX_QUEUE_SIZE; i++) {
        if (s_rx_dma_bufs[i]) {
            free(s_rx_dma_bufs[i]);
            s_rx_dma_bufs[i] = NULL;
        }
    }

    if (s_parse_buf) {
        free(s_parse_buf);
        s_parse_buf = NULL;
    }
}
