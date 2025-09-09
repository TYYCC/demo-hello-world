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

// ACK负载结构
#pragma pack(push, 1)
typedef struct {
    uint8_t original_frame_type;  // 原始帧类型
    uint8_t ack_status;          // ACK状态
    uint8_t data_len;            // 附加数据长度
    uint8_t data[];              // 附加数据
} ack_payload_t;
#pragma pack(pop)

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

/**
 * @brief 初始化TCP命令服务器
 * 
 * @param config 服务器配置，NULL使用默认配置
 * @return true 初始化成功
 * @return false 初始化失败
 */
bool tcp_command_server_init(const tcp_cmd_server_config_t* config);

/**
 * @brief 启动TCP命令服务器
 * 
 * @param task_name 任务名称，NULL使用默认名称
 * @param stack_size 任务栈大小，0使用默认大小
 * @param task_priority 任务优先级
 * @param cmd_handler 命令处理回调函数
 * @param conn_callback 连接状态变更回调函数，可为NULL
 * @return true 启动成功
 * @return false 启动失败
 */
bool tcp_command_server_start(const char* task_name, uint32_t stack_size, UBaseType_t task_priority,
                             tcp_cmd_handler_t cmd_handler, tcp_cmd_connection_callback_t conn_callback);

/**
 * @brief 停止TCP命令服务器
 */
void tcp_command_server_stop(void);

/**
 * @brief 销毁TCP命令服务器
 */
void tcp_command_server_destroy(void);

/**
 * @brief 获取服务器状态
 * 
 * @return tcp_cmd_server_state_t 当前状态
 */
tcp_cmd_server_state_t tcp_command_server_get_state(void);

/**
 * @brief 获取服务器统计信息
 * 
 * @return const tcp_cmd_server_stats_t* 统计信息指针
 */
const tcp_cmd_server_stats_t* tcp_command_server_get_stats(void);

/**
 * @brief 获取客户端信息
 * 
 * @param client_index 客户端索引
 * @return const tcp_cmd_client_info_t* 客户端信息指针，失败返回NULL
 */
const tcp_cmd_client_info_t* tcp_command_server_get_client_info(uint32_t client_index);

/**
 * @brief 重置统计信息
 */
void tcp_command_server_reset_stats(void);

/**
 * @brief 打印服务器状态
 */
void tcp_command_server_print_status(void);

/**
 * @brief 获取活跃客户端数量
 * 
 * @return uint32_t 活跃客户端数量
 */
uint32_t tcp_command_server_get_active_client_count(void);

/**
 * @brief 向指定客户端发送数据
 * 
 * @param client_index 客户端索引
 * @param frame_type 帧类型
 * @param payload 负载数据
 * @param payload_len 负载长度
 * @return true 发送成功
 * @return false 发送失败
 */
bool tcp_command_server_send_to_client(uint32_t client_index, uint8_t frame_type, 
                                      const uint8_t* payload, size_t payload_len);

/**
 * @brief 向所有客户端广播数据
 * 
 * @param frame_type 帧类型
 * @param payload 负载数据
 * @param payload_len 负载长度
 * @return uint32_t 成功发送的客户端数量
 */
uint32_t tcp_command_server_broadcast(uint8_t frame_type, const uint8_t* payload, size_t payload_len);

/**
 * @brief 创建ACK帧
 * 
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @param original_frame_type 原始帧类型
 * @param ack_status ACK状态
 * @param response_data 响应数据，可为NULL
 * @param response_len 响应数据长度
 * @return size_t 创建的帧长度，失败返回0
 */
size_t tcp_command_server_create_ack_frame(uint8_t* buffer, size_t buffer_size,
                                          uint8_t original_frame_type, ack_status_t ack_status,
                                          const uint8_t* response_data, size_t response_len);

#ifdef __cplusplus
}
#endif

#endif // TCP_COMMAND_SERVER_H