#ifndef TCP_SERVER_SERVICE_H
#define TCP_SERVER_SERVICE_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Initializes the TCP server service.
 *
 * @param port The port number for the server to listen on.
 * @return esp_err_t ESP_OK on success, or an error code on failure.
 */
esp_err_t tcp_server_service_init(int port);

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