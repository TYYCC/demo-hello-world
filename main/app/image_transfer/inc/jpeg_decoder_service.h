#ifndef JPEG_DECODER_SERVICE_H
#define JPEG_DECODER_SERVICE_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// 数据回调函数类型定义
typedef void (*jpeg_decoder_callback_t)(const uint8_t* data, size_t length, 
                                       uint16_t width, uint16_t height, void* context);

/**
 * @brief 初始化JPEG解码服务
 * @param data_callback 数据接收回调函数
 * @param context 回调函数上下文
 * @return esp_err_t 执行结果
 */
esp_err_t jpeg_decoder_service_init(jpeg_decoder_callback_t data_callback, void* context);

/**
 * @brief 停止JPEG解码服务
 */
void jpeg_decoder_service_deinit(void);

/**
 * @brief 检查JPEG解码服务是否正在运行
 * @return bool 运行状态
 */
bool jpeg_decoder_service_is_running(void);

/**
 * @brief 获取JPEG解码服务事件组句柄
 * @return EventGroupHandle_t 事件组句柄
 */
EventGroupHandle_t jpeg_decoder_service_get_event_group(void);

/**
 * @brief 帧数据解锁（允许新的数据写入）
 */
void jpeg_decoder_service_frame_unlock(void);

/**
 * @brief 获取解码后的帧数据（UI线程使用）
 * 
 * @param data 输出参数，指向解码后的帧数据
 * @param width 输出参数，帧宽度
 * @param height 输出参数，帧高度
 * @return bool 是否成功获取帧数据
 */
bool jpeg_decoder_service_get_frame_data(uint8_t** data, int* width, int* height);

/**
 * @brief Processes a chunk of JPEG compressed data.
 *
 * This function is called to feed JPEG data into the service for decompression.
 *
 * @param data Pointer to the data buffer.
 * @param length Length of the data in bytes.
 */
void jpeg_decoder_service_process_data(const uint8_t* data, size_t length);

#endif // JPEG_DECODER_SERVICE_H