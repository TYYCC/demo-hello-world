#ifndef RAW_DATA_SERVICE_H
#define RAW_DATA_SERVICE_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// 数据回调函数类型定义
typedef void (*raw_data_callback_t)(const uint8_t* data, size_t length, void* context);

/**
 * @brief 初始化原始数据服务
 * @param data_callback 数据接收回调函数
 * @param context 回调函数上下文
 * @return esp_err_t 执行结果
 */
esp_err_t raw_data_service_init(raw_data_callback_t data_callback, void* context);

/**
 * @brief 停止原始数据服务
 */
void raw_data_service_deinit(void);

/**
 * @brief 检查原始数据服务是否正在运行
 * @return bool 运行状态
 */
bool raw_data_service_is_running(void);

/**
 * @brief 获取原始数据服务事件组句柄
 * @return EventGroupHandle_t 事件组句柄
 */
EventGroupHandle_t raw_data_service_get_event_group(void);

/**
 * @brief 获取最新帧数据
 * @param frame_buffer 帧数据缓冲区指针
 * @param frame_size 帧数据大小
 * @return bool 是否成功获取
 */
bool raw_data_service_get_latest_frame(uint8_t** frame_buffer, size_t* frame_size);

/**
 * @brief 帧数据解锁（允许新的数据写入）
 */
void raw_data_service_frame_unlock(void);

#endif // RAW_DATA_SERVICE_H