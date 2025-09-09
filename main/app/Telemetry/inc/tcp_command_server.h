/**
 * @file tcp_command_server.h
 * @brief TCP命令服务器 - 支持命令传输、解析和ACK应答
 * @author TidyCraze
 * @date 2025-01-27
 */

#ifndef TCP_COMMAND_SERVER_H
#define TCP_COMMAND_SERVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "telemetry_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

// 默认配置
#define TCP_CMD_SERVER_DEFAULT_PORT 8081
#define TCP_CMD_SERVER_MAX_CLIENTS 4
#define TCP_CMD_SERVER_BUFFER_SIZE 512
#define TCP_CMD_SERVER_ACK_TIMEOUT_MS 5000
#define TCP_CMD_SERVER_RECV_TIMEOUT_MS 1000

// ACK帧类型
#define FRAME_TYPE_ACK 0x07

// ACK状态码
typedef enum {
    ACK_STATUS_SUCCESS = 0x00,
    ACK_STATUS_ERROR = 0x01,
    ACK_STATUS_INVALID_COMMAND = 0x02,
    ACK_STATUS_PARAMETER_ERROR = 0x03,
    ACK_STATUS_BUSY = 0x04,
    ACK_STATUS_TIMEOUT = 0x05
} ack_status_t;

// 服务器状态
typedef enum {
    TCP_CMD_SERVER_STATE_STOPPED = 0,
    TCP_CMD_SERVER_STATE_STARTING,
    TCP_CMD_SERVER_STATE_RUNNING,
    TCP_CMD_SERVER_STATE_ERROR
} tcp_cmd_server_state_t;

// 客户端连接状态
typedef enum {
    TCP_CMD_CLIENT_DISCONNECTED = 0,
    TCP_CMD_CLIENT_CONNECTED,
    TCP_CMD_CLIENT_AUTHENTICATED
} tcp_cmd_client_state_t;

// ACK负载结构已在telemetry_protocol.h中定义

// 命令处理结果
typedef struct {
    ack_status_t status;
    uint8_t response_data[64];   // 响应数据
    size_t response_len;         // 响应数据长度
} command_result_t;

// 客户端信息
typedef struct {
    int socket_fd;
    tcp_cmd_client_state_t state;
    uint32_t connection_time;
    uint32_t last_activity_time;
    uint8_t recv_buffer[TCP_CMD_SERVER_BUFFER_SIZE];
    size_t recv_buffer_len;
} tcp_cmd_client_info_t;

// 服务器统计信息
typedef struct {
    uint32_t total_connections;
    uint32_t active_clients;
    uint32_t commands_received;
    uint32_t commands_processed;
    uint32_t ack_sent;
    uint32_t errors;
} tcp_cmd_server_stats_t;

// 服务器配置
typedef struct {
    uint16_t server_port;
    uint8_t max_clients;
    uint32_t ack_timeout_ms;
    uint32_t recv_timeout_ms;
} tcp_cmd_server_config_t;

// 命令处理回调函数类型
typedef command_result_t (*tcp_cmd_handler_t)(uint8_t frame_type, const uint8_t* payload, size_t payload_len, uint32_t client_index);

// 连接状态变更回调
typedef void (*tcp_cmd_connection_callback_t)(uint32_t client_index, bool connected);

// 所有服务器端功能已移除，仅保留必要的类型定义

#ifdef __cplusplus
}
#endif

#endif // TCP_COMMAND_SERVER_H