/**
 * @file tcp_server_hb.h
 * @brief TCP心跳服务器实现 - 接收心跳包
 * @author TidyCraze
 * @date 2025-09-05
 */

#ifndef TCP_SERVER_HB_H
#define TCP_SERVER_HB_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ----------------- 配置宏定义 -----------------
#define TCP_SERVER_HB_DEFAULT_PORT 7878    // 默认心跳端口
#define TCP_SERVER_HB_MAX_CLIENTS 5        // 最大客户端连接数
#define TCP_SERVER_HB_BUFFER_SIZE 256      // 接收缓冲区大小
#define TCP_SERVER_HB_RECV_TIMEOUT_MS 5000 // 接收超时时间：5秒

// ----------------- 连接状态枚举 -----------------
typedef enum {
    TCP_SERVER_HB_STATE_STOPPED = 0, // 已停止
    TCP_SERVER_HB_STATE_STARTING,    // 启动中
    TCP_SERVER_HB_STATE_RUNNING,     // 运行中
    TCP_SERVER_HB_STATE_STOPPING,    // 停止中
    TCP_SERVER_HB_STATE_ERROR        // 错误状态
} tcp_server_hb_state_t;

// ----------------- 客户端状态枚举 -----------------
typedef enum {
    TCP_SERVER_HB_CLIENT_DISCONNECTED = 0, // 未连接
    TCP_SERVER_HB_CLIENT_CONNECTED,        // 已连接
    TCP_SERVER_HB_CLIENT_ERROR             // 错误状态
} tcp_server_hb_client_state_t;

// ----------------- 心跳包负载结构 -----------------
typedef struct __attribute__((packed)) {
    uint8_t device_status; // 设备状态
    uint32_t timestamp;    // 时间戳
} tcp_server_hb_payload_t;

// ----------------- 客户端信息结构 -----------------
typedef struct {
    int socket_fd;                        // 客户端套接字
    tcp_server_hb_client_state_t state;   // 客户端状态
    uint32_t last_heartbeat_time;         // 最后心跳时间
    uint32_t connection_time;             // 连接建立时间
    tcp_server_hb_payload_t last_payload; // 最后接收的心跳包负载
} tcp_server_hb_client_info_t;

// ----------------- 服务器统计信息 -----------------
typedef struct {
    uint32_t total_connections;        // 总连接数
    uint32_t active_clients;           // 当前活跃客户端数
    uint32_t heartbeat_received_count; // 已接收心跳包数量
    uint32_t heartbeat_failed_count;   // 心跳包解析失败数量
    uint32_t last_heartbeat_time;      // 最后心跳时间
} tcp_server_hb_stats_t;

// ----------------- 服务器配置 -----------------
typedef struct {
    uint16_t server_port;          // 服务器端口
    uint32_t max_clients;          // 最大客户端数
    uint32_t heartbeat_timeout_ms; // 心跳超时时间
} tcp_server_hb_config_t;

// ----------------- 回调函数类型 -----------------
/**
 * @brief 心跳包接收回调函数
 * @param client_index 客户端索引
 * @param payload 心跳包负载
 */
typedef void (*tcp_server_hb_callback_t)(uint32_t client_index,
                                         const tcp_server_hb_payload_t* payload);

/**
 * @brief 客户端连接状态变更回调函数
 * @param client_index 客户端索引
 * @param connected true=连接，false=断开
 */
typedef void (*tcp_server_hb_connection_callback_t)(uint32_t client_index, bool connected);

// ----------------- 函数声明 -----------------

/**
 * @brief 初始化TCP心跳服务器
 * @param config 服务器配置（NULL使用默认配置）
 * @return true 初始化成功，false 初始化失败
 */
bool tcp_server_hb_init(const tcp_server_hb_config_t* config);

/**
 * @brief 启动TCP心跳服务器
 * @param task_name 任务名称
 * @param stack_size 任务栈大小
 * @param task_priority 任务优先级
 * @param heartbeat_callback 心跳包接收回调函数
 * @param connection_callback 连接状态变更回调函数
 * @return true 启动成功，false 启动失败
 */
bool tcp_server_hb_start(const char* task_name, uint32_t stack_size, UBaseType_t task_priority,
                         tcp_server_hb_callback_t heartbeat_callback,
                         tcp_server_hb_connection_callback_t connection_callback);

/**
 * @brief 停止TCP心跳服务器
 */
void tcp_server_hb_stop(void);

/**
 * @brief 销毁TCP心跳服务器，释放所有资源
 */
void tcp_server_hb_destroy(void);

/**
 * @brief 获取当前服务器状态
 * @return 服务器状态
 */
tcp_server_hb_state_t tcp_server_hb_get_state(void);

/**
 * @brief 获取服务器统计信息
 * @return 统计信息指针
 */
const tcp_server_hb_stats_t* tcp_server_hb_get_stats(void);

/**
 * @brief 获取指定客户端信息
 * @param client_index 客户端索引
 * @return 客户端信息指针，NULL表示无效索引
 */
const tcp_server_hb_client_info_t* tcp_server_hb_get_client_info(uint32_t client_index);

/**
 * @brief 重置统计信息
 */
void tcp_server_hb_reset_stats(void);

/**
 * @brief 打印服务器状态信息
 */
void tcp_server_hb_print_status(void);

/**
 * @brief 获取活跃客户端数量
 * @return 活跃客户端数量
 */
uint32_t tcp_server_hb_get_active_client_count(void);

/**
 * @brief 广播图传配置命令给所有连接的客户端
 * 
 * @param config_data 配置数据
 * @param config_len 配置数据长度
 * @return int 成功发送的客户端数量，失败返回负数
 */
int tcp_server_hb_send_image_config(const uint8_t* config_data, size_t config_len);

#ifdef __cplusplus
}
#endif

#endif // TCP_SERVER_HB_H
