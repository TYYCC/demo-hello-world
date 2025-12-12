#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动TCP服务器
 *
 * @return esp_err_t 启动结果
 */
esp_err_t tcp_server_start(void);

/**
 * @brief 停止TCP服务器
 *
 */
void tcp_server_stop(void);

#ifdef __cplusplus
}
#endif

#endif // TCP_SERVER_H