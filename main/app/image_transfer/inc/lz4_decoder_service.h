/*
 * @Author: tidycraze 2595256284@qq.com
 * @Date: 2025-09-10 13:37:58
 * @LastEditors: tidycraze 2595256284@qq.com
 * @LastEditTime: 2025-09-10 13:45:12
 * @FilePath: \demo-hello-world\main\app\image_transfer\inc\lz4_decoder_service.h
 * @Description: LZ4解码服务头文件，用于处理LZ4压缩图像数据
 * 
 */
#ifndef LZ4_DECODER_SERVICE_H
#define LZ4_DECODER_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

// LZ4解码器回调函数类型定义
typedef void (*lz4_decoder_callback_t)(const uint8_t* data, size_t length, 
                                     uint16_t width, uint16_t height, void* context);

// LZ4解码器事件组位定义
extern EventGroupHandle_t lz4_decoder_event_group;
#define LZ4_DECODER_DATA_READY_BIT BIT0  // 数据就绪位
#define LZ4_DECODER_STOP_BIT       BIT1  // 停止位

/**
 * @brief 初始化LZ4解码服务
 * @param display_queue 显示队列句柄
 * @return 初始化成功返回true，失败返回false
 */
bool lz4_decoder_service_init(QueueHandle_t display_queue);

/**
 * @brief 反初始化LZ4解码服务
 */
void lz4_decoder_service_deinit(void);

/**
 * @brief 检查LZ4解码服务是否正在运行
 * @return 服务正在运行返回true，否则返回false
 */
bool lz4_decoder_service_is_running(void);

/**
 * @brief 处理LZ4压缩数据
 * @param data LZ4数据指针
 * @param data_len 数据长度
 * @param width 图像宽度（从协议头部获取）
 * @param height 图像高度（从协议头部获取）
 */
void lz4_decoder_service_process_data(const uint8_t *data, uint32_t data_len, uint16_t width, uint16_t height);

/**
 * @brief 解锁当前帧，允许处理下一帧
 */
void lz4_decoder_service_frame_unlock(void);

/**
 * @brief 获取LZ4解码器事件组句柄
 * @return 事件组句柄
 */
EventGroupHandle_t lz4_decoder_service_get_event_group(void);

#endif // LZ4_DECODER_SERVICE_H