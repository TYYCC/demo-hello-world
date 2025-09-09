#ifndef UI_MAPPING_SERVICE_H
#define UI_MAPPING_SERVICE_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include "raw_data_service.h"
#include "lz4_decoder_service.h"
#include "jpeg_decoder_service.h"
#include <stdbool.h>
#include <stdint.h>

// UI数据类型枚举
typedef enum {
    UI_DATA_TYPE_RAW,   // 原始数据
    UI_DATA_TYPE_LZ4,   // LZ4解压缩数据
    UI_DATA_TYPE_JPEG   // JPEG解码数据
} ui_data_type_t;

// UI映射回调函数类型定义
typedef void (*ui_mapping_callback_t)(const uint8_t* data, size_t length, 
                                     uint16_t width, uint16_t height, 
                                     ui_data_type_t data_type, void* context);

/**
 * @brief 初始化UI映射服务
 * @param ui_callback UI数据回调函数
 * @param context 回调函数上下文
 * @return esp_err_t 执行结果
 */
esp_err_t ui_mapping_service_init(ui_mapping_callback_t ui_callback, void* context);

/**
 * @brief 停止UI映射服务
 */
void ui_mapping_service_deinit(void);

/**
 * @brief 检查UI映射服务是否正在运行
 * @return bool 运行状态
 */
bool ui_mapping_service_is_running(void);

/**
 * @brief 获取UI映射服务事件组句柄
 * @return EventGroupHandle_t 事件组句柄
 */
EventGroupHandle_t ui_mapping_service_get_event_group(void);

/**
 * @brief 获取原始数据映射回调
 * @return raw_data_callback_t 原始数据回调函数
 */
raw_data_callback_t ui_mapping_service_get_raw_callback(void);

/**
 * @brief 获取LZ4数据映射回调
 * @return lz4_decoder_callback_t LZ4数据回调函数
 */
lz4_decoder_callback_t ui_mapping_service_get_lz4_callback(void);

/**
 * @brief 获取JPEG数据映射回调
 * @return jpeg_decoder_callback_t JPEG数据回调函数
 */
jpeg_decoder_callback_t ui_mapping_service_get_jpeg_callback(void);

/**
 * @brief 帧数据解锁（允许新的数据写入）
 */
void ui_mapping_service_frame_unlock(void);

#endif // UI_MAPPING_SERVICE_H