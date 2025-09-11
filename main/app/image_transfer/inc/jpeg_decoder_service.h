/*
 * @Author: tidycraze 2595256284@qq.com
 * @Date: 2025-09-10 13:37:58
 * @LastEditors: tidycraze 2595256284@qq.com
 * @LastEditTime: 2025-09-10 13:45:12
 * @FilePath: \demo-hello-world\main\app\image_transfer\inc\jpeg_decoder_service.h
 * @Description: JPEG解码服务头文件，用于处理JPEG图像解码
 * 
 */
#ifndef JPEG_DECODER_SERVICE_H
#define JPEG_DECODER_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

// JPEG解码器回调函数类型定义
typedef void (*jpeg_decoder_callback_t)(const uint8_t* data, size_t length, 
                                      uint16_t width, uint16_t height, void* context);

// JPEG解码器事件组位定义
extern EventGroupHandle_t jpeg_decoder_event_group;
#define JPEG_DECODER_DATA_READY_BIT BIT0  // 数据就绪位
#define JPEG_DECODER_STOP_BIT       BIT1  // 停止位

/**
 * @brief 初始化JPEG解码服务
 * @param data_callback 数据回调函数
 * @param context 回调上下文
 * @param display_queue 显示队列句柄
 * @return 初始化成功返回ESP_OK，失败返回错误码
 */
esp_err_t jpeg_decoder_service_init(jpeg_decoder_callback_t data_callback, void* context, QueueHandle_t display_queue);

/**
 * @brief 反初始化JPEG解码服务
 */
void jpeg_decoder_service_deinit(void);

/**
 * @brief 检查JPEG解码服务是否正在运行
 * @return 服务正在运行返回true，否则返回false
 */
bool jpeg_decoder_service_is_running(void);

/**
 * @brief 处理JPEG数据
 * @param data JPEG数据指针
 * @param length 数据长度
 */
void jpeg_decoder_service_process_data(const uint8_t *data, size_t length);

/**
 * @brief 解锁当前帧，允许处理下一帧
 */
void jpeg_decoder_service_frame_unlock(void);

/**
 * @brief 获取JPEG解码器事件组句柄
 * @return 事件组句柄
 */
EventGroupHandle_t jpeg_decoder_service_get_event_group(void);

#endif // JPEG_DECODER_SERVICE_H