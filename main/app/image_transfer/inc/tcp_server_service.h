#ifndef TCP_SERVER_SERVICE_H
#define TCP_SERVER_SERVICE_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// 数据回调函数类型定义
typedef void (*tcp_server_callback_t)(const uint8_t* data, size_t length, void* context);

/**
 * @brief 初始化TCP服务器服务
 * @param port 服务器监听端口
 * @param data_callback 数据接收回调函数
 * @param context 回调函数上下文
 * @return esp_err_t 执行结果
 */
esp_err_t tcp_server_service_init(int port, tcp_server_callback_t data_callback, void* context);

/**
 * @brief 停止TCP服务器服务
 */
void tcp_server_service_deinit(void);

/**
 * @brief 检查TCP服务器是否正在运行
 * @return bool 运行状态
 */
bool tcp_server_service_is_running(void);

/**
 * @brief 检查是否有客户端连接
 * @return bool 连接状态
 */
bool tcp_server_service_is_connected(void);

/**
 * @brief 获取TCP服务器事件组句柄
 * @return EventGroupHandle_t 事件组句柄
 */
EventGroupHandle_t tcp_server_service_get_event_group(void);

#endif // TCP_SERVER_SERVICE_H