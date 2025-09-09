#ifndef TCP_CLIENT_TELEMETRY_H
#define TCP_CLIENT_TELEMETRY_H

#include "tcp_common_protocol.h"
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#ifdef __cplusplus
extern "C" {
#endif

// ----------------- 配置常量 -----------------
#define TCP_CLIENT_TELEMETRY_DEFAULT_PORT 6667  // 默认遥测端口
#define TCP_CLIENT_TELEMETRY_RECV_BUFFER_SIZE 1024 // 接收缓冲区大小
#define TCP_CLIENT_TELEMETRY_FRAME_BUFFER_SIZE 256 // 帧缓冲区大小
#define TCP_CLIENT_TELEMETRY_RECONNECT_DELAY_MS 5000 // 重连延时
#define TCP_CLIENT_TELEMETRY_SEND_TIMEOUT_MS 5000    // 发送超时时间
#define TCP_CLIENT_TELEMETRY_RECV_TIMEOUT_MS 1000    // 接收超时时间

// ----------------- 新增：遥控数据结构 -----------------
typedef struct {
    uint8_t channel_count;
    uint16_t channel_values[8]; // 最多支持8通道
} remote_control_data_t;

// ----------------- 新增：遥控命令回调函数类型 -----------------
typedef void (*remote_control_callback_t)(const remote_control_data_t *rc_data);

// 客户端状态 -----------------
typedef enum {
    TCP_CLIENT_TELEMETRY_STATE_DISCONNECTED = 0,
    TCP_CLIENT_TELEMETRY_STATE_CONNECTING,
    TCP_CLIENT_TELEMETRY_STATE_CONNECTED,
    TCP_CLIENT_TELEMETRY_STATE_ERROR
} tcp_client_telemetry_state_t;

// 遥测统计信息 -----------------
typedef struct {
    uint32_t telemetry_sent_count;           // 已发送遥测包数量
    uint32_t telemetry_failed_count;         // 发送失败遥测包数量
    uint32_t connection_count;               // 连接次数
    uint32_t reconnection_count;             // 重连次数
    uint64_t last_telemetry_time;            // 最后一次遥测时间戳（毫秒）
    uint64_t connection_start_time;          // 连接开始时间戳（毫秒）
    uint64_t total_connected_time;           // 总连接时间（毫秒）
    uint32_t bytes_sent;                     // 已发送字节数
    uint32_t bytes_received;                 // 已接收字节数
} tcp_client_telemetry_stats_t;

// 遥测客户端配置 -----------------
typedef struct {
    char server_ip[16];                      // 服务器IP地址
    uint16_t server_port;                    // 服务器端口
    uint32_t reconnect_delay_ms;             // 重连延时
    uint32_t send_timeout_ms;                // 发送超时时间
    uint32_t recv_timeout_ms;                // 接收超时时间
    bool auto_reconnect_enabled;             // 是否启用自动重连
} tcp_client_telemetry_config_t;

// 模拟遥测数据 -----------------
typedef struct {
    uint16_t voltage_mv;
    uint16_t current_ma;
    int16_t roll_deg;
    int16_t pitch_deg;
    int16_t yaw_deg;
    int32_t altitude_cm;
} tcp_client_telemetry_sim_data_t;

// 函数声明 -----------------

/**
 * @brief 初始化TCP遥测客户端
 * @param server_ip 服务器IP地址
 * @param server_port 服务器端口
 * @return true 初始化成功，false 初始化失败
 */
bool tcp_client_telemetry_init(const char *server_ip, uint16_t server_port);

/**
 * @brief 启动TCP遥测客户端任务
 * @param task_name 任务名称
 * @param stack_size 任务栈大小
 * @param task_priority 任务优先级
 * @return true 启动成功，false 启动失败
 */
bool tcp_client_telemetry_start(const char *task_name, uint32_t stack_size, UBaseType_t task_priority);

/**
 * @brief 停止TCP遥测客户端
 */
void tcp_client_telemetry_stop(void);

/**
 * @brief 销毁TCP遥测客户端，释放所有资源
 */
void tcp_client_telemetry_destroy(void);

/**
 * @brief 连接到遥测服务器
 * @return true 连接成功，false 连接失败
 */
bool tcp_client_telemetry_connect(void);

/**
 * @brief 断开遥测连接
 */
void tcp_client_telemetry_disconnect(void);

/**
 * @brief 获取客户端状态
 * @return 当前状态
 */
tcp_client_telemetry_state_t tcp_client_telemetry_get_state(void);

/**
 * @brief 发送遥测数据
 * @param telemetry_data 遥测数据指针
 * @return true 发送成功，false 发送失败
 */
bool tcp_client_telemetry_send_data(const telemetry_data_payload_t *telemetry_data);

/**
 * @brief 处理接收到的数据
 * @return true 继续处理，false 连接断开
 */
bool tcp_client_telemetry_process_received_data(void);

/**
 * @brief 获取遥测统计信息
 * @return 统计信息指针
 */
const tcp_client_telemetry_stats_t* tcp_client_telemetry_get_stats(void);

/**
 * @brief 设置自动重连开关
 * @param enabled 是否启用自动重连
 */
void tcp_client_telemetry_set_auto_reconnect(bool enabled);

/**
 * @brief 检查连接是否健康
 * @return true 连接健康，false 连接异常
 */
bool tcp_client_telemetry_is_connection_healthy(void);

/**
 * @brief 重置统计信息
 */
void tcp_client_telemetry_reset_stats(void);

/**
 * @brief 打印状态信息
 */
void tcp_client_telemetry_print_status(void);

/**
 * @brief 更新模拟遥测数据
 * @param sim_data 模拟数据指针
 */
void tcp_client_telemetry_update_sim_data(tcp_client_telemetry_sim_data_t *sim_data);

/**
 * @brief 打印接收到的帧信息
 * @param buffer 协议帧缓冲区指针
 * @param buffer_len 缓冲区长度
 */
void tcp_client_telemetry_print_received_frame(const uint8_t *buffer, uint16_t buffer_len);

/**
 * @brief 注册遥控命令回调函数
 * 
 * @param callback 回调函数指针
 */
void tcp_client_telemetry_register_rc_callback(remote_control_callback_t callback);

/**
 * @brief 初始化PWM控制器
 * 
 * @param frequency PWM频率(Hz)，0表示使用默认频率
 * @return true 初始化成功
 * @return false 初始化失败
 */
bool tcp_client_telemetry_init_pwm(uint32_t frequency);

#ifdef __cplusplus
}
#endif

#endif // TCP_CLIENT_TELEMETRY_H