#ifndef TCP_CLIENT_HB_H
#define TCP_CLIENT_HB_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "tcp_common_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

// ----------------- 配置宏定义 -----------------
#define TCP_CLIENT_HB_DEFAULT_PORT 7878         // 默认心跳端口
#define TCP_CLIENT_HB_INTERVAL_MS 30000         // 心跳发送间隔：30秒
#define TCP_CLIENT_HB_RECONNECT_INTERVAL_MS 5000 // 自动重连间隔：5秒
#define TCP_CLIENT_HB_TIMEOUT_MS 90000          // 心跳超时时间：90秒
#define TCP_CLIENT_HB_SEND_TIMEOUT_MS 5000      // 发送超时时间：5秒
#define TCP_CLIENT_HB_RECV_TIMEOUT_MS 1000      // 接收超时时间：1秒

// 设备状态定义
#define TCP_CLIENT_HB_DEVICE_STATUS_IDLE 0x00    // 空闲
#define TCP_CLIENT_HB_DEVICE_STATUS_RUNNING 0x01 // 正常运行
#define TCP_CLIENT_HB_DEVICE_STATUS_ERROR 0x02   // 错误

// ----------------- 连接状态枚举 -----------------
typedef enum {
    TCP_CLIENT_HB_STATE_DISCONNECTED = 0,    // 未连接
    TCP_CLIENT_HB_STATE_CONNECTING,          // 连接中
    TCP_CLIENT_HB_STATE_CONNECTED,           // 已连接
    TCP_CLIENT_HB_STATE_RECONNECTING,        // 重连中
    TCP_CLIENT_HB_STATE_ERROR                // 错误状态
} tcp_client_hb_state_t;

// ----------------- 心跳统计信息 -----------------
typedef struct {
    uint32_t heartbeat_sent_count;           // 已发送心跳包数量
    uint32_t heartbeat_failed_count;         // 发送失败心跳包数量
    uint32_t connection_count;               // 连接次数
    uint32_t reconnection_count;             // 重连次数
    uint64_t last_heartbeat_time;            // 最后一次心跳时间戳（毫秒）
    uint64_t connection_start_time;          // 连接开始时间戳（毫秒）
    uint64_t total_connected_time;           // 总连接时间（毫秒）
} tcp_client_hb_stats_t;

// ----------------- 心跳客户端配置 -----------------
typedef struct {
    char server_ip[16];                      // 服务器IP地址
    uint16_t server_port;                    // 服务器端口
    uint32_t heartbeat_interval_ms;          // 心跳间隔
    uint32_t reconnect_interval_ms;          // 重连间隔
    uint32_t heartbeat_timeout_ms;           // 心跳超时时间
    uint8_t device_status;                   // 当前设备状态
    bool auto_reconnect_enabled;            // 是否启用自动重连
} tcp_client_hb_config_t;

// ----------------- 函数声明 -----------------

/**
 * @brief 初始化TCP心跳客户端
 * @param server_ip 服务器IP地址
 * @param server_port 服务器端口
 * @return true 初始化成功，false 初始化失败
 */
bool tcp_client_hb_init(const char *server_ip, uint16_t server_port);

/**
 * @brief 启动TCP心跳客户端任务
 * @param task_name 任务名称
 * @param stack_size 任务栈大小
 * @param task_priority 任务优先级
 * @return true 启动成功，false 启动失败
 */
bool tcp_client_hb_start(const char *task_name, uint32_t stack_size, UBaseType_t task_priority);

/**
 * @brief 停止TCP心跳客户端
 */
void tcp_client_hb_stop(void);

/**
 * @brief 销毁TCP心跳客户端，释放所有资源
 */
void tcp_client_hb_destroy(void);

/**
 * @brief 获取当前连接状态
 * @return 当前连接状态
 */
tcp_client_hb_state_t tcp_client_hb_get_state(void);

/**
 * @brief 获取心跳统计信息
 * @return 统计信息指针
 */
const tcp_client_hb_stats_t* tcp_client_hb_get_stats(void);

/**
 * @brief 设置设备状态
 * @param status 设备状态
 */
void tcp_client_hb_set_device_status(uint8_t status);

/**
 * @brief 立即发送心跳包
 * @return true 发送成功，false 发送失败
 */
bool tcp_client_hb_send_now(void);

/**
 * @brief 立即重连
 * @return true 重连成功，false 重连失败
 */
bool tcp_client_hb_reconnect_now(void);

/**
 * @brief 设置自动重连开关
 * @param enabled 是否启用自动重连
 */
void tcp_client_hb_set_auto_reconnect(bool enabled);

/**
 * @brief 检查连接是否健康
 * @return true 连接健康，false 连接异常
 */
bool tcp_client_hb_is_connection_healthy(void);

/**
 * @brief 重置统计信息
 */
void tcp_client_hb_reset_stats(void);

/**
 * @brief 打印状态信息
 */
void tcp_client_hb_print_status(void);

#ifdef __cplusplus
}
#endif

#endif // TCP_CLIENT_HB_H