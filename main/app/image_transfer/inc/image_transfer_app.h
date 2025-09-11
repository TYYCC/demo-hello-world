/*
 * @Author: tidycraze 2595256284@qq.com
 * @Date: 2025-09-10 13:37:58
 * @LastEditors: tidycraze 2595256284@qq.com
 * @LastEditTime: 2025-09-10 13:45:12
 * @FilePath: \demo-hello-world\main\app\image_transfer\inc\image_transfer_app.h
 * @Description: 图像传输主应用头文件，管理JPEG和LZ4解码服务
 * 
 */
#ifndef IMAGE_TRANSFER_APP_H
#define IMAGE_TRANSFER_APP_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include "../../inc/settings_manager.h"

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