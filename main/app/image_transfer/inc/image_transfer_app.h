#ifndef IMAGE_TRANSFER_APP_H
#define IMAGE_TRANSFER_APP_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include "../../inc/settings_manager.h"  // 使用统一的枚举定义

// 使用 settings_manager.h 中定义的 image_transfer_mode_t
// 不再重复定义枚举

/**
 * @brief 初始化图像传输应用程序
 * @param initial_mode 初始传输模式
 * @return esp_err_t 执行结果
 */
esp_err_t image_transfer_app_init(image_transfer_mode_t initial_mode);

/**
 * @brief 设置图像传输模式
 * @param mode 传输模式
 * @return esp_err_t 执行结果
 */
esp_err_t image_transfer_app_set_mode(image_transfer_mode_t mode);

/**
 * @brief 获取当前图像传输模式
 * @return image_transfer_mode_t 当前模式
 */
image_transfer_mode_t image_transfer_app_get_mode(void);

/**
 * @brief 启动TCP服务器
 * @param port TCP端口号
 * @return esp_err_t 执行结果
 */
esp_err_t image_transfer_app_start_tcp_server(uint16_t port);

/**
 * @brief 停止TCP服务器
 */
void image_transfer_app_stop_tcp_server(void);

/**
 * @brief 检查TCP服务器是否正在运行
 * @return bool 运行状态
 */
bool image_transfer_app_is_tcp_server_running(void);

/**
 * @brief 停止图像传输应用程序
 */
void image_transfer_app_deinit(void);

/**
 * @brief 检查应用程序是否正在运行
 * @return bool 运行状态
 */
bool image_transfer_app_is_running(void);

#endif // IMAGE_TRANSFER_APP_H