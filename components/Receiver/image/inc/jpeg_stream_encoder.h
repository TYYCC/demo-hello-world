/*
 * @Author: tidycraze 2595256284@qq.com
 * @Date: 2025-09-01 13:24:54
 * @LastEditors: tidycraze 2595256284@qq.com
 * @LastEditTime: 2025-09-04 12:15:17
 * @FilePath: \demo-hello-world\components\Receiver\inc\jpeg_stream_encoder.h
 * @Description: 
 * 
 */
#ifndef JPEG_STREAM_ENCODER_H
#define JPEG_STREAM_ENCODER_H

#include "esp_err.h"
#include "esp_jpeg_common.h"
#include "esp_jpeg_enc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdint.h>
#include <stddef.h>

// JPEG编码参数配置
#define JPEG_ENC_WIDTH 240
#define JPEG_ENC_HEIGHT 188
#define JPEG_ENC_QUALITY 70
#define JPEG_ENC_SRC_TYPE JPEG_PIXEL_FORMAT_RGBA
#define JPEG_ENC_SUBSAMPLE JPEG_SUBSAMPLE_422

// JPEG数据块消息结构
typedef struct {
    uint8_t* data;
    size_t len;
} jpeg_chunk_msg_t;

// JPEG编码器回调函数类型
typedef void (*jpeg_output_callback_t)(const uint8_t* jpeg_data, size_t jpeg_len);

/**
 * @brief 初始化JPEG流编码器
 * @param output_callback 编码完成后的回调函数，用于处理编码后的JPEG数据
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t jpeg_stream_encoder_init(jpeg_output_callback_t output_callback);

/**
 * @brief 启动JPEG编码任务
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t jpeg_stream_encoder_start(void);

/**
 * @brief 停止JPEG编码任务并清理资源
 */
void jpeg_stream_encoder_stop(void);

/**
 * @brief 向JPEG编码器发送数据块
 * @param data 数据指针
 * @param len 数据长度
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t jpeg_stream_encoder_feed_data(const uint8_t* data, size_t len);

/**
 * @brief 获取JPEG编码器队列句柄
 * @return 队列句柄，如果未初始化则返回NULL
 */
QueueHandle_t jpeg_stream_encoder_get_queue(void);

/**
 * @brief 设置JPEG编码质量
 * @param quality 质量值 (1-100)
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t jpeg_stream_encoder_set_quality(uint8_t quality);

/**
 * @brief 获取当前JPEG编码质量
 * @return 当前质量值
 */
uint8_t jpeg_stream_encoder_get_quality(void);

#endif // JPEG_STREAM_ENCODER_H
