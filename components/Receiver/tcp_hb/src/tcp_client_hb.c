/**
 * @file tcp_client_hb.c
 * @brief TCP心跳客户端实现
 * @author TidyCraze
 * @date 2025-09-05
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/select.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_system.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"

#include "tcp_client_hb.h"
#include "tcp_common_protocol.h"

static const char *TAG = "TCP_CLIENT_HB";

// ----------------- 心跳客户端结构体 -----------------
typedef struct {
    tcp_client_hb_config_t config;           // 配置信息
    tcp_client_hb_state_t state;             // 当前状态
    tcp_client_hb_stats_t stats;             // 统计信息
    int socket_fd;                           // TCP套接字文件描述符
    TimerHandle_t heartbeat_timer;           // 心跳定时器
    TimerHandle_t reconnect_timer;           // 重连定时器
    TaskHandle_t heartbeat_task_handle;      // 心跳任务句柄
    bool is_initialized;                     // 是否已初始化
    bool is_running;                         // 是否正在运行
} tcp_client_hb_manager_t;

// ----------------- 全局变量 -----------------
static tcp_client_hb_manager_t g_hb_client = {0};
static bool g_hb_client_initialized = false;

// ----------------- 内部函数声明 -----------------
static bool tcp_client_hb_connect_internal(void);
static void tcp_client_hb_disconnect_internal(void);
static bool tcp_client_hb_send_packet(void);
static uint16_t tcp_client_hb_create_packet(uint8_t *buffer, uint16_t buffer_size);
static void tcp_client_hb_timer_callback(TimerHandle_t xTimer);
static void tcp_client_hb_reconnect_timer_callback(TimerHandle_t xTimer);
static void tcp_client_hb_task_function(void *pvParameters);
static void tcp_client_hb_set_state(tcp_client_hb_state_t new_state);
static uint64_t tcp_client_hb_get_timestamp_ms(void);
static bool tcp_client_hb_is_socket_valid(void);
static void tcp_client_hb_update_stats_on_connect(void);
static void tcp_client_hb_update_stats_on_disconnect(void);

// ----------------- 内部函数实现 -----------------
static uint64_t tcp_client_hb_get_timestamp_ms(void) {
    return (uint64_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void tcp_client_hb_set_state(tcp_client_hb_state_t new_state) {
    if (g_hb_client.state != new_state) {
        tcp_client_hb_state_t old_state = g_hb_client.state;
        g_hb_client.state = new_state;
        
        ESP_LOGI(TAG, "状态变更: %d -> %d", old_state, new_state);
        
        // 状态变更时的特殊处理
        if (new_state == TCP_CLIENT_HB_STATE_CONNECTED) {
            tcp_client_hb_update_stats_on_connect();
        } else if (old_state == TCP_CLIENT_HB_STATE_CONNECTED) {
            tcp_client_hb_update_stats_on_disconnect();
        }
    }
}

static bool tcp_client_hb_is_socket_valid(void) {
    return (g_hb_client.socket_fd >= 0);
}

static void tcp_client_hb_update_stats_on_connect(void) {
    g_hb_client.stats.connection_count++;
    g_hb_client.stats.connection_start_time = tcp_client_hb_get_timestamp_ms();
    ESP_LOGI(TAG, "连接建立，连接次数: %lu", g_hb_client.stats.connection_count);
}

static void tcp_client_hb_update_stats_on_disconnect(void) {
    uint64_t current_time = tcp_client_hb_get_timestamp_ms();
    if (g_hb_client.stats.connection_start_time > 0) {
        uint64_t session_time = current_time - g_hb_client.stats.connection_start_time;
        g_hb_client.stats.total_connected_time += session_time;
        ESP_LOGI(TAG, "连接断开，本次连接时长: %llu ms，总连接时长: %llu ms", 
                session_time, g_hb_client.stats.total_connected_time);
    }
}

static uint16_t tcp_client_hb_create_packet(uint8_t *buffer, uint16_t buffer_size) {
    // 使用通用协议函数创建心跳帧
    heartbeat_payload_t payload;
    payload.device_status = g_hb_client.config.device_status;
    payload.timestamp = (uint32_t)(tcp_client_hb_get_timestamp_ms() / 1000); // 转换为秒
    
    return create_heartbeat_frame(buffer, buffer_size, payload.device_status, payload.timestamp);
}

static bool tcp_client_hb_send_packet(void) {
    if (!tcp_client_hb_is_socket_valid()) {
        ESP_LOGE(TAG, "套接字无效，无法发送心跳包");
        return false;
    }

    uint8_t buffer[256];
    uint16_t packet_length = tcp_client_hb_create_packet(buffer, sizeof(buffer));
    
    if (packet_length == 0) {
        ESP_LOGE(TAG, "创建心跳包失败");
        g_hb_client.stats.heartbeat_failed_count++;
        return false;
    }

    int sent_bytes = send(g_hb_client.socket_fd, buffer, packet_length, 0);
    if (sent_bytes != packet_length) {
        ESP_LOGE(TAG, "发送心跳包失败: %s", strerror(errno));
        g_hb_client.stats.heartbeat_failed_count++;
        return false;
    }

    // 发送成功后，尝试接收数据来检测连接状态
    uint8_t recv_buffer[1];
    int recv_result = recv(g_hb_client.socket_fd, recv_buffer, sizeof(recv_buffer), MSG_DONTWAIT);
    if (recv_result == 0) {
        // 连接已被对方关闭
        ESP_LOGW(TAG, "检测到连接已断开（recv返回0）");
        g_hb_client.stats.heartbeat_failed_count++;
        return false;
    } else if (recv_result < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        // 发生了真正的错误（EAGAIN和EWOULDBLOCK是正常的，表示没有数据可读）
        ESP_LOGW(TAG, "检测到连接错误: %s", strerror(errno));
        g_hb_client.stats.heartbeat_failed_count++;
        return false;
    }

    g_hb_client.stats.heartbeat_sent_count++;
    g_hb_client.stats.last_heartbeat_time = tcp_client_hb_get_timestamp_ms();
    
    return true;
}

static bool tcp_client_hb_connect_internal(void) {
    if (tcp_client_hb_is_socket_valid()) {
        ESP_LOGW(TAG, "套接字已连接");
        return true;
    }

    tcp_client_hb_set_state(TCP_CLIENT_HB_STATE_CONNECTING);
    ESP_LOGI(TAG, "正在连接到 %s:%d", g_hb_client.config.server_ip, g_hb_client.config.server_port);

    // 创建套接字
    g_hb_client.socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_hb_client.socket_fd < 0) {
        ESP_LOGE(TAG, "创建套接字失败: %s", strerror(errno));
        tcp_client_hb_set_state(TCP_CLIENT_HB_STATE_RECONNECTING);
        return false;
    }

    // 设置套接字选项
    struct timeval timeout;
    timeout.tv_sec = TCP_CLIENT_HB_SEND_TIMEOUT_MS / 1000;
    timeout.tv_usec = (TCP_CLIENT_HB_SEND_TIMEOUT_MS % 1000) * 1000;
    setsockopt(g_hb_client.socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
    timeout.tv_sec = TCP_CLIENT_HB_RECV_TIMEOUT_MS / 1000;
    timeout.tv_usec = (TCP_CLIENT_HB_RECV_TIMEOUT_MS % 1000) * 1000;
    setsockopt(g_hb_client.socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    // 启用TCP Keep-Alive机制
    int keepalive = 1;
    setsockopt(g_hb_client.socket_fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
    
    // 设置Keep-Alive参数（如果系统支持）
    #ifdef TCP_KEEPIDLE
    int keepidle = 30;  // 30秒后开始发送keep-alive探测
    setsockopt(g_hb_client.socket_fd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
    #endif
    
    #ifdef TCP_KEEPINTVL
    int keepintvl = 5;  // 探测间隔5秒
    setsockopt(g_hb_client.socket_fd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
    #endif
    
    #ifdef TCP_KEEPCNT
    int keepcnt = 3;    // 最多3次探测失败后断开连接
    setsockopt(g_hb_client.socket_fd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));
    #endif

    // 配置服务器地址
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(g_hb_client.config.server_port);
    
    if (inet_pton(AF_INET, g_hb_client.config.server_ip, &server_addr.sin_addr) <= 0) {
        ESP_LOGE(TAG, "无效的IP地址: %s", g_hb_client.config.server_ip);
        close(g_hb_client.socket_fd);
        g_hb_client.socket_fd = -1;
        tcp_client_hb_set_state(TCP_CLIENT_HB_STATE_RECONNECTING);
        return false;
    }

    // 设置连接超时（使用非阻塞模式）
    int flags = fcntl(g_hb_client.socket_fd, F_GETFL, 0);
    fcntl(g_hb_client.socket_fd, F_SETFL, flags | O_NONBLOCK);
    
    // 连接到服务器
    int connect_result = connect(g_hb_client.socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (connect_result < 0) {
        if (errno == EINPROGRESS) {
            // 非阻塞连接正在进行中，使用select等待完成
            fd_set write_fds;
            struct timeval timeout_val;
            timeout_val.tv_sec = 5;  // 5秒连接超时
            timeout_val.tv_usec = 0;
            
            FD_ZERO(&write_fds);
            FD_SET(g_hb_client.socket_fd, &write_fds);
            
            int select_result = select(g_hb_client.socket_fd + 1, NULL, &write_fds, NULL, &timeout_val);
            if (select_result > 0) {
                // 检查连接是否成功
                int error = 0;
                socklen_t len = sizeof(error);
                if (getsockopt(g_hb_client.socket_fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
                    ESP_LOGE(TAG, "连接失败: %s", strerror(error ? error : errno));
                    close(g_hb_client.socket_fd);
                    g_hb_client.socket_fd = -1;
                    tcp_client_hb_set_state(TCP_CLIENT_HB_STATE_RECONNECTING);
                    return false;
                }
            } else if (select_result == 0) {
                ESP_LOGE(TAG, "连接超时");
                close(g_hb_client.socket_fd);
                g_hb_client.socket_fd = -1;
                tcp_client_hb_set_state(TCP_CLIENT_HB_STATE_RECONNECTING);
                return false;
            } else {
                ESP_LOGE(TAG, "select失败: %s", strerror(errno));
                close(g_hb_client.socket_fd);
                g_hb_client.socket_fd = -1;
                tcp_client_hb_set_state(TCP_CLIENT_HB_STATE_RECONNECTING);
                return false;
            }
        } else {
            ESP_LOGE(TAG, "连接失败: %s", strerror(errno));
            close(g_hb_client.socket_fd);
            g_hb_client.socket_fd = -1;
            tcp_client_hb_set_state(TCP_CLIENT_HB_STATE_RECONNECTING);
            return false;
        }
    }
    
    // 恢复阻塞模式
    fcntl(g_hb_client.socket_fd, F_SETFL, flags);

    tcp_client_hb_set_state(TCP_CLIENT_HB_STATE_CONNECTED);
    ESP_LOGI(TAG, "连接成功");

    // 连接成功后，立即发送第一个心跳包
    if (!tcp_client_hb_send_packet()) {
        ESP_LOGW(TAG, "发送初始心跳包失败");
    }

    // 连接成功后，启动心跳定时器，停止重连定时器
    if (g_hb_client.reconnect_timer) {
        xTimerStop(g_hb_client.reconnect_timer, 0);
    }
    if (g_hb_client.heartbeat_timer) {
        xTimerStart(g_hb_client.heartbeat_timer, 0);
    }
    
    return true;
}

static void tcp_client_hb_disconnect_internal(void) {
    if (tcp_client_hb_is_socket_valid()) {
        close(g_hb_client.socket_fd);
        g_hb_client.socket_fd = -1;
        ESP_LOGI(TAG, "连接已断开");
    }
    tcp_client_hb_set_state(TCP_CLIENT_HB_STATE_DISCONNECTED);
}

static void tcp_client_hb_timer_callback(TimerHandle_t xTimer) {
    (void)xTimer;
    
    if (g_hb_client.state == TCP_CLIENT_HB_STATE_CONNECTED) {
        if (!tcp_client_hb_send_packet()) {
            ESP_LOGW(TAG, "心跳发送失败，可能需要重连");
            if (g_hb_client.config.auto_reconnect_enabled) {
                tcp_client_hb_disconnect_internal(); // 先断开连接
                tcp_client_hb_set_state(TCP_CLIENT_HB_STATE_RECONNECTING);
                xTimerStop(g_hb_client.heartbeat_timer, 0); // 停止心跳定时器
                xTimerStart(g_hb_client.reconnect_timer, 0); // 启动重连定时器
            }
        }
    }
}

static void tcp_client_hb_reconnect_timer_callback(TimerHandle_t xTimer) {
    (void)xTimer;
    
    if (g_hb_client.state == TCP_CLIENT_HB_STATE_RECONNECTING) {
        ESP_LOGI(TAG, "尝试重连...");
        g_hb_client.stats.reconnection_count++;
        
        if (tcp_client_hb_connect_internal()) {
            // 重连成功
            ESP_LOGI(TAG, "重连成功");
        } else {
            ESP_LOGW(TAG, "重连失败，等待下次重试");
        }
    }
}

static void tcp_client_hb_task_function(void *pvParameters) {
    (void)pvParameters;
    
    ESP_LOGI(TAG, "心跳任务启动");
    
    while (g_hb_client.is_running) {
        // 检查连接健康状态
        if (g_hb_client.state == TCP_CLIENT_HB_STATE_CONNECTED) {
            uint64_t current_time = tcp_client_hb_get_timestamp_ms();
            if (g_hb_client.stats.last_heartbeat_time > 0 && 
                (current_time - g_hb_client.stats.last_heartbeat_time) > g_hb_client.config.heartbeat_timeout_ms) {
                ESP_LOGW(TAG, "心跳超时，触发重连");
                if (g_hb_client.config.auto_reconnect_enabled) {
                    tcp_client_hb_disconnect_internal();
                    tcp_client_hb_set_state(TCP_CLIENT_HB_STATE_RECONNECTING);
                    xTimerStop(g_hb_client.heartbeat_timer, 0);
                    xTimerStart(g_hb_client.reconnect_timer, 0);
                }
            }
        }
        
        // 任务延迟，避免占用过多CPU
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    ESP_LOGI(TAG, "心跳任务结束");
    vTaskDelete(NULL);
}

// ----------------- 公共接口实现 -----------------

bool tcp_client_hb_init(const char *server_ip, uint16_t server_port) {
    if (g_hb_client_initialized) {
        ESP_LOGW(TAG, "心跳客户端已初始化");
        return true;
    }

    if (!server_ip) {
        ESP_LOGE(TAG, "服务器IP地址不能为空");
        return false;
    }

    // 初始化配置
    memset(&g_hb_client, 0, sizeof(g_hb_client));
    strncpy(g_hb_client.config.server_ip, server_ip, sizeof(g_hb_client.config.server_ip) - 1);
    g_hb_client.config.server_port = (server_port > 0) ? server_port : TCP_CLIENT_HB_DEFAULT_PORT;
    g_hb_client.config.heartbeat_interval_ms = TCP_CLIENT_HB_INTERVAL_MS;
    g_hb_client.config.reconnect_interval_ms = TCP_CLIENT_HB_RECONNECT_INTERVAL_MS;
    g_hb_client.config.heartbeat_timeout_ms = TCP_CLIENT_HB_TIMEOUT_MS;
    g_hb_client.config.device_status = TCP_CLIENT_HB_DEVICE_STATUS_IDLE;
    g_hb_client.config.auto_reconnect_enabled = true;
    
    g_hb_client.socket_fd = -1;
    g_hb_client.state = TCP_CLIENT_HB_STATE_DISCONNECTED;
    g_hb_client.is_initialized = true;
    g_hb_client.is_running = false;

    // 创建定时器
    g_hb_client.heartbeat_timer = xTimerCreate(
        "HB_Timer",
        pdMS_TO_TICKS(g_hb_client.config.heartbeat_interval_ms),
        pdTRUE,  // 自动重载
        NULL,
        tcp_client_hb_timer_callback
    );
    
    g_hb_client.reconnect_timer = xTimerCreate(
        "HB_Reconnect_Timer",
        pdMS_TO_TICKS(g_hb_client.config.reconnect_interval_ms),
        pdTRUE,  // 自动重载
        NULL,
        tcp_client_hb_reconnect_timer_callback
    );

    if (!g_hb_client.heartbeat_timer || !g_hb_client.reconnect_timer) {
        ESP_LOGE(TAG, "创建定时器失败");
        tcp_client_hb_destroy();
        return false;
    }

    g_hb_client_initialized = true;
    ESP_LOGI(TAG, "心跳客户端初始化成功，服务器: %s:%d", server_ip, g_hb_client.config.server_port);
    
    return true;
}

bool tcp_client_hb_start(const char *task_name, uint32_t stack_size, UBaseType_t task_priority) {
    if (!g_hb_client_initialized) {
        ESP_LOGE(TAG, "心跳客户端未初始化");
        return false;
    }

    if (g_hb_client.is_running) {
        ESP_LOGW(TAG, "心跳客户端已在运行");
        return true;
    }

    // 设置默认参数
    const char *name = task_name ? task_name : "tcp_hb_task";
    uint32_t stack = (stack_size > 0) ? stack_size : 4096;
    UBaseType_t priority = task_priority;

    // 创建任务
    BaseType_t result = xTaskCreate(
        tcp_client_hb_task_function,
        name,
        stack,
        NULL,
        priority,
        &g_hb_client.heartbeat_task_handle
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "创建心跳任务失败");
        return false;
    }

    g_hb_client.is_running = true;
    
    // 设置初始状态为重连中，让任务异步处理连接
    if (g_hb_client.config.auto_reconnect_enabled) {
        tcp_client_hb_set_state(TCP_CLIENT_HB_STATE_RECONNECTING);
        xTimerStart(g_hb_client.reconnect_timer, 0);
        ESP_LOGI(TAG, "心跳客户端启动成功，将异步尝试连接");
    } else {
        ESP_LOGI(TAG, "心跳客户端启动成功，自动重连已禁用");
    }
    
    return true;
}

void tcp_client_hb_stop(void) {
    if (!g_hb_client.is_running) {
        return;
    }

    g_hb_client.is_running = false;
    
    // 停止定时器
    if (g_hb_client.heartbeat_timer) {
        xTimerStop(g_hb_client.heartbeat_timer, portMAX_DELAY);
    }
    if (g_hb_client.reconnect_timer) {
        xTimerStop(g_hb_client.reconnect_timer, portMAX_DELAY);
    }
    
    // 断开连接
    tcp_client_hb_disconnect_internal();
    
    // 等待任务结束
    if (g_hb_client.heartbeat_task_handle) {
        // 等待任务真正结束，最多等待5秒
        uint32_t wait_count = 0;
        while (eTaskGetState(g_hb_client.heartbeat_task_handle) != eDeleted && wait_count < 50) {
            vTaskDelay(pdMS_TO_TICKS(100));
            wait_count++;
        }
        g_hb_client.heartbeat_task_handle = NULL;
    }
    
    ESP_LOGI(TAG, "心跳客户端已停止");
}

void tcp_client_hb_destroy(void) {
    tcp_client_hb_stop();
    
    // 删除定时器
    if (g_hb_client.heartbeat_timer) {
        xTimerDelete(g_hb_client.heartbeat_timer, portMAX_DELAY);
        g_hb_client.heartbeat_timer = NULL;
    }
    if (g_hb_client.reconnect_timer) {
        xTimerDelete(g_hb_client.reconnect_timer, portMAX_DELAY);
        g_hb_client.reconnect_timer = NULL;
    }
    
    memset(&g_hb_client, 0, sizeof(g_hb_client));
    g_hb_client.socket_fd = -1;
    g_hb_client_initialized = false;
    
    ESP_LOGI(TAG, "心跳客户端已销毁");
}

tcp_client_hb_state_t tcp_client_hb_get_state(void) {
    return g_hb_client.state;
}

const tcp_client_hb_stats_t* tcp_client_hb_get_stats(void) {
    return &g_hb_client.stats;
}

void tcp_client_hb_set_device_status(uint8_t status) {
    if (status <= TCP_CLIENT_HB_DEVICE_STATUS_ERROR) {
        g_hb_client.config.device_status = status;
        ESP_LOGI(TAG, "设备状态更新为: %d", status);
    } else {
        ESP_LOGW(TAG, "无效的设备状态: %d", status);
    }
}

bool tcp_client_hb_send_now(void) {
    if (!g_hb_client_initialized) {
        ESP_LOGE(TAG, "心跳客户端未初始化");
        return false;
    }
    
    return tcp_client_hb_send_packet();
}

bool tcp_client_hb_reconnect_now(void) {
    if (!g_hb_client_initialized) {
        ESP_LOGE(TAG, "心跳客户端未初始化");
        return false;
    }
    
    xTimerStop(g_hb_client.heartbeat_timer, 0);
    xTimerStop(g_hb_client.reconnect_timer, 0);
    tcp_client_hb_disconnect_internal();
    return tcp_client_hb_connect_internal();
}

void tcp_client_hb_set_auto_reconnect(bool enabled) {
    g_hb_client.config.auto_reconnect_enabled = enabled;
}

bool tcp_client_hb_is_connection_healthy(void) {
    if (g_hb_client.state != TCP_CLIENT_HB_STATE_CONNECTED) {
        return false;
    }
    
    uint64_t current_time = tcp_client_hb_get_timestamp_ms();
    return (g_hb_client.stats.last_heartbeat_time > 0 && 
            (current_time - g_hb_client.stats.last_heartbeat_time) <= g_hb_client.config.heartbeat_timeout_ms);
}

void tcp_client_hb_reset_stats(void) {
    memset(&g_hb_client.stats, 0, sizeof(g_hb_client.stats));
}

void tcp_client_hb_print_status(void) {
    ESP_LOGI(TAG, "=== 心跳客户端状态 ===");
    ESP_LOGI(TAG, "状态: %d", g_hb_client.state);
    ESP_LOGI(TAG, "服务器: %s:%d", g_hb_client.config.server_ip, g_hb_client.config.server_port);
}