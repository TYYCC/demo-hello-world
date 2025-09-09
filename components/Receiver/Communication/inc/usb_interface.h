#pragma once

#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 处理USB命令字符串
 * 
 * @param command USB命令字符串，格式: "SET <setting> <value>"
 * @return esp_err_t 执行结果
 */
esp_err_t usb_process_command(const char* command);

/**
 * @brief 获取当前设置状态
 * 
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @return esp_err_t 执行结果
 */
esp_err_t usb_get_settings_status(char* buffer, size_t buffer_size);

/**
 * @brief 保存当前设置到NVS
 * 
 * @return esp_err_t 执行结果
 */
esp_err_t usb_save_settings(void);

/**
 * @brief 恢复默认设置
 * 
 * @return esp_err_t 执行结果
 */
esp_err_t usb_reset_settings(void);

#ifdef __cplusplus
}
#endif