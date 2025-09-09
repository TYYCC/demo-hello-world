/**
 * @file tcp_server_hb.c
 * @brief TCP心跳服务器实现 - 接收心跳包
 * @author TidyCraze
 * @date 2025-09-05
 */

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>


#include "tcp_server_hb.h"
#include "telemetry_protocol.h"

static const char* TAG = "TCP_SERVER_HB";

// ----------------- 服务器管理器结构体 -----------------
typedef struct {
    tcp_server_hb_config_t config;                                  // 配置信息
    tcp_server_hb_state_t state;                                    // 当前状态
    tcp_server_hb_stats_t stats;                                    // 统计信息
    tcp_server_hb_client_info_t clients[TCP_SERVER_HB_MAX_CLIENTS]; // 客户端信息数组
    int server_socket_fd;                                           // 服务器套接字
    TaskHandle_t server_task_handle;                                // 服务器任务句柄
    tcp_server_hb_callback_t heartbeat_callback;                    // 心跳包回调
    tcp_server_hb_connection_callback_t connection_callback;        // 连接回调
    bool is_initialized;                                            // 是否已初始化
    bool is_running;                                                // 是否正在运行
} tcp_server_hb_manager_t;

// ----------------- 全局变量 -----------------
static tcp_server_hb_manager_t g_hb_server = {0};
static bool g_hb_server_initialized = false;

// ----------------- 内部函数声明 -----------------
static bool tcp_server_hb_bind_and_listen(void);
static void tcp_server_hb_disconnect_client(uint32_t client_index);
static void tcp_server_hb_handle_client_data(uint32_t client_index);
static bool tcp_server_hb_accept_new_connection(void);
static void tcp_server_hb_set_state(tcp_server_hb_state_t new_state);
static uint32_t tcp_server_hb_get_timestamp_ms(void);
static int tcp_server_hb_find_free_client_slot(void);
static void tcp_server_hb_cleanup_disconnected_clients(void);
static bool tcp_server_hb_validate_frame(const uint8_t* buffer, uint16_t buffer_size,
                                         tcp_server_hb_payload_t* payload);

// ----------------- 内部函数实现 -----------------

static uint32_t tcp_server_hb_get_timestamp_ms(void) {
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void tcp_server_hb_set_state(tcp_server_hb_state_t new_state) {
    if (g_hb_server.state != new_state) {
        tcp_server_hb_state_t old_state = g_hb_server.state;
        g_hb_server.state = new_state;

        ESP_LOGI(TAG, "服务器状态变更: %d -> %d", old_state, new_state);
    }
}

static int tcp_server_hb_find_free_client_slot(void) {
    for (int i = 0; i < TCP_SERVER_HB_MAX_CLIENTS; i++) {
        if (g_hb_server.clients[i].state == TCP_SERVER_HB_CLIENT_DISCONNECTED) {
            return i;
        }
    }
    return -1;
}

static void tcp_server_hb_disconnect_client(uint32_t client_index) {
    if (client_index >= TCP_SERVER_HB_MAX_CLIENTS) {
        return;
    }

    tcp_server_hb_client_info_t* client = &g_hb_server.clients[client_index];

    if (client->socket_fd >= 0) {
        close(client->socket_fd);
        client->socket_fd = -1;
        ESP_LOGI(TAG, "客户端 %d 已断开", client_index);
    }

    client->state = TCP_SERVER_HB_CLIENT_DISCONNECTED;

    // 调用连接状态变更回调
    if (g_hb_server.connection_callback) {
        g_hb_server.connection_callback(client_index, false);
    }

    g_hb_server.stats.active_clients--;
}

static bool tcp_server_hb_bind_and_listen(void) {
    // 创建服务器套接字
    g_hb_server.server_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_hb_server.server_socket_fd < 0) {
        ESP_LOGE(TAG, "创建服务器套接字失败: %s", strerror(errno));
        return false;
    }

    // 设置套接字选项
    int opt = 1;
    if (setsockopt(g_hb_server.server_socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        ESP_LOGE(TAG, "设置SO_REUSEADDR失败: %s", strerror(errno));
        close(g_hb_server.server_socket_fd);
        g_hb_server.server_socket_fd = -1;
        return false;
    }

    // 绑定地址和端口
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(g_hb_server.config.server_port);

    if (bind(g_hb_server.server_socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) <
        0) {
        ESP_LOGE(TAG, "绑定端口 %d 失败: %s", g_hb_server.config.server_port, strerror(errno));
        close(g_hb_server.server_socket_fd);
        g_hb_server.server_socket_fd = -1;
        return false;
    }

    // 开始监听
    if (listen(g_hb_server.server_socket_fd, 5) < 0) {
        ESP_LOGE(TAG, "监听端口 %d 失败: %s", g_hb_server.config.server_port, strerror(errno));
        close(g_hb_server.server_socket_fd);
        g_hb_server.server_socket_fd = -1;
        return false;
    }

    ESP_LOGI(TAG, "服务器开始监听端口 %d", g_hb_server.config.server_port);
    return true;
}

static bool tcp_server_hb_accept_new_connection(void) {
    if (g_hb_server.stats.active_clients >= g_hb_server.config.max_clients) {
        ESP_LOGW(TAG, "已达到最大客户端连接数 (%d)，拒绝新连接", g_hb_server.config.max_clients);
        return false;
    }

    int client_slot = tcp_server_hb_find_free_client_slot();
    if (client_slot < 0) {
        ESP_LOGW(TAG, "没有可用的客户端槽位");
        return false;
    }

    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    int client_fd =
        accept(g_hb_server.server_socket_fd, (struct sockaddr*)&client_addr, &client_addr_len);
    if (client_fd < 0) {
        ESP_LOGE(TAG, "接受连接失败: %s", strerror(errno));
        return false;
    }

    // 设置客户端套接字为非阻塞模式
    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

    // 设置接收超时
    struct timeval timeout;
    timeout.tv_sec = TCP_SERVER_HB_RECV_TIMEOUT_MS / 1000;
    timeout.tv_usec = (TCP_SERVER_HB_RECV_TIMEOUT_MS % 1000) * 1000;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    // 初始化客户端信息
    tcp_server_hb_client_info_t* client = &g_hb_server.clients[client_slot];
    client->socket_fd = client_fd;
    client->state = TCP_SERVER_HB_CLIENT_CONNECTED;
    client->last_heartbeat_time = tcp_server_hb_get_timestamp_ms();
    client->connection_time = client->last_heartbeat_time;
    memset(&client->last_payload, 0, sizeof(client->last_payload));

    g_hb_server.stats.total_connections++;
    g_hb_server.stats.active_clients++;

    ESP_LOGI(TAG, "新客户端连接: 槽位=%d, 套接字=%d, 地址=%s:%d", client_slot, client_fd,
             inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

    // 调用连接状态变更回调
    if (g_hb_server.connection_callback) {
        g_hb_server.connection_callback(client_slot, true);
    }

    return true;
}

static bool tcp_server_hb_validate_frame(const uint8_t* buffer, uint16_t buffer_size,
                                         tcp_server_hb_payload_t* payload) {
    parsed_frame_t parsed_frame;

    // 使用telemetry_protocol解析帧
    size_t frame_len = telemetry_protocol_parse_frame(buffer, buffer_size, &parsed_frame);

    if (frame_len == 0) {
        ESP_LOGW(TAG, "帧解析失败");
        return false;
    }

    if (!parsed_frame.crc_ok) {
        ESP_LOGW(TAG, "CRC校验失败");
        return false;
    }

    if (parsed_frame.header.type != FRAME_TYPE_HEARTBEAT) {
        ESP_LOGW(TAG, "不是心跳帧类型: %d", parsed_frame.header.type);
        return false;
    }

    if (parsed_frame.payload_len < sizeof(tcp_server_hb_payload_t)) {
        ESP_LOGW(TAG, "心跳包负载长度不足: %d", parsed_frame.payload_len);
        return false;
    }

    // 复制负载数据
    memcpy(payload, parsed_frame.payload, sizeof(tcp_server_hb_payload_t));

    return true;
}

static void tcp_server_hb_handle_client_data(uint32_t client_index) {
    if (client_index >= TCP_SERVER_HB_MAX_CLIENTS) {
        return;
    }

    tcp_server_hb_client_info_t* client = &g_hb_server.clients[client_index];

    if (client->state != TCP_SERVER_HB_CLIENT_CONNECTED) {
        return;
    }

    uint8_t buffer[TCP_SERVER_HB_BUFFER_SIZE];
    int recv_len = recv(client->socket_fd, buffer, sizeof(buffer), 0);

    if (recv_len == 0) {
        // 客户端主动断开连接
        ESP_LOGI(TAG, "客户端 %d 主动断开连接", client_index);
        tcp_server_hb_disconnect_client(client_index);
        return;
    } else if (recv_len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 没有数据可读，这是正常的
            return;
        } else {
            // 发生错误
            ESP_LOGW(TAG, "客户端 %d 接收数据错误: %s", client_index, strerror(errno));
            tcp_server_hb_disconnect_client(client_index);
            return;
        }
    }

    // 处理接收到的数据
    tcp_server_hb_payload_t payload;
    if (tcp_server_hb_validate_frame(buffer, recv_len, &payload)) {
        // 帧验证成功
        client->last_heartbeat_time = tcp_server_hb_get_timestamp_ms();
        client->last_payload = payload;

        g_hb_server.stats.heartbeat_received_count++;
        g_hb_server.stats.last_heartbeat_time = client->last_heartbeat_time;

        ESP_LOGI(TAG, "接收到客户端 %d 的心跳包: 状态=%d, 时间戳=%u", client_index,
                 payload.device_status, payload.timestamp);

        // 调用心跳包回调
        if (g_hb_server.heartbeat_callback) {
            g_hb_server.heartbeat_callback(client_index, &payload);
        }
    } else {
        // 帧验证失败
        g_hb_server.stats.heartbeat_failed_count++;
        ESP_LOGW(TAG, "客户端 %d 发送的帧验证失败", client_index);
    }
}

static void tcp_server_hb_cleanup_disconnected_clients(void) {
    uint32_t current_time = tcp_server_hb_get_timestamp_ms();

    for (int i = 0; i < TCP_SERVER_HB_MAX_CLIENTS; i++) {
        tcp_server_hb_client_info_t* client = &g_hb_server.clients[i];

        if (client->state == TCP_SERVER_HB_CLIENT_CONNECTED) {
            // 检查心跳超时
            if (current_time - client->last_heartbeat_time >
                g_hb_server.config.heartbeat_timeout_ms) {
                ESP_LOGW(TAG, "客户端 %d 心跳超时 (%u ms)，断开连接", i,
                         current_time - client->last_heartbeat_time);
                tcp_server_hb_disconnect_client(i);
            }
        }
    }
}

// ----------------- 服务器任务 -----------------
static void tcp_server_hb_task(void* pvParameters) {
    (void)pvParameters;

    ESP_LOGI(TAG, "心跳服务器任务启动");

    // 创建fd_set用于select
    fd_set read_fds;
    struct timeval timeout;

    while (g_hb_server.is_running) {
        // 清理断开的客户端
        tcp_server_hb_cleanup_disconnected_clients();

        // 准备select
        FD_ZERO(&read_fds);
        int max_fd = g_hb_server.server_socket_fd;

        // 添加服务器套接字
        FD_SET(g_hb_server.server_socket_fd, &read_fds);

        // 添加所有客户端套接字
        for (int i = 0; i < TCP_SERVER_HB_MAX_CLIENTS; i++) {
            if (g_hb_server.clients[i].state == TCP_SERVER_HB_CLIENT_CONNECTED) {
                FD_SET(g_hb_server.clients[i].socket_fd, &read_fds);
                if (g_hb_server.clients[i].socket_fd > max_fd) {
                    max_fd = g_hb_server.clients[i].socket_fd;
                }
            }
        }

        // 设置超时时间
        timeout.tv_sec = 1; // 1秒超时
        timeout.tv_usec = 0;

        // 等待事件
        int select_result = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

        if (select_result < 0) {
            ESP_LOGE(TAG, "select失败: %s", strerror(errno));
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        } else if (select_result == 0) {
            // 超时，继续循环
            continue;
        }

        // 检查是否有新连接
        if (FD_ISSET(g_hb_server.server_socket_fd, &read_fds)) {
            tcp_server_hb_accept_new_connection();
        }

        // 检查客户端数据
        for (int i = 0; i < TCP_SERVER_HB_MAX_CLIENTS; i++) {
            if (g_hb_server.clients[i].state == TCP_SERVER_HB_CLIENT_CONNECTED &&
                FD_ISSET(g_hb_server.clients[i].socket_fd, &read_fds)) {
                tcp_server_hb_handle_client_data(i);
            }
        }
    }

    ESP_LOGI(TAG, "心跳服务器任务结束");
    g_hb_server.server_task_handle = NULL;
    vTaskDelete(NULL);
}

// ----------------- 公共接口实现 -----------------

bool tcp_server_hb_init(const tcp_server_hb_config_t* config) {
    if (g_hb_server_initialized) {
        ESP_LOGW(TAG, "心跳服务器已初始化");
        return true;
    }

    // 初始化配置
    memset(&g_hb_server, 0, sizeof(g_hb_server));

    if (config != NULL) {
        g_hb_server.config = *config;
    } else {
        // 使用默认配置
        g_hb_server.config.server_port = TCP_SERVER_HB_DEFAULT_PORT;
        g_hb_server.config.max_clients = TCP_SERVER_HB_MAX_CLIENTS;
        g_hb_server.config.heartbeat_timeout_ms = 90000; // 90秒超时
    }

    // 初始化客户端数组
    for (int i = 0; i < TCP_SERVER_HB_MAX_CLIENTS; i++) {
        g_hb_server.clients[i].socket_fd = -1;
        g_hb_server.clients[i].state = TCP_SERVER_HB_CLIENT_DISCONNECTED;
    }

    g_hb_server.server_socket_fd = -1;
    g_hb_server.state = TCP_SERVER_HB_STATE_STOPPED;
    g_hb_server.is_initialized = true;
    g_hb_server.is_running = false;

    g_hb_server_initialized = true;
    ESP_LOGI(TAG, "心跳服务器初始化成功，端口: %d", g_hb_server.config.server_port);

    return true;
}

bool tcp_server_hb_start(const char* task_name, uint32_t stack_size, UBaseType_t task_priority,
                         tcp_server_hb_callback_t heartbeat_callback,
                         tcp_server_hb_connection_callback_t connection_callback) {
    if (!g_hb_server_initialized) {
        ESP_LOGE(TAG, "心跳服务器未初始化");
        return false;
    }

    if (g_hb_server.is_running) {
        ESP_LOGW(TAG, "心跳服务器已在运行");
        return true;
    }

    g_hb_server.heartbeat_callback = heartbeat_callback;
    g_hb_server.connection_callback = connection_callback;

    // 绑定并监听端口
    if (!tcp_server_hb_bind_and_listen()) {
        ESP_LOGE(TAG, "绑定和监听失败");
        return false;
    }

    // 设置默认参数
    const char* name = task_name ? task_name : "tcp_hb_server";
    uint32_t stack = (stack_size > 0) ? stack_size : 4096;

    // 创建任务
    BaseType_t result = xTaskCreate(tcp_server_hb_task, name, stack, NULL, task_priority,
                                    &g_hb_server.server_task_handle);

    if (result != pdPASS) {
        ESP_LOGE(TAG, "创建心跳服务器任务失败");
        close(g_hb_server.server_socket_fd);
        g_hb_server.server_socket_fd = -1;
        return false;
    }

    g_hb_server.is_running = true;
    tcp_server_hb_set_state(TCP_SERVER_HB_STATE_RUNNING);

    ESP_LOGI(TAG, "心跳服务器启动成功，监听端口: %d", g_hb_server.config.server_port);
    return true;
}

void tcp_server_hb_stop(void) {
    if (!g_hb_server.is_running) {
        return;
    }

    g_hb_server.is_running = false;

    // 断开所有客户端连接
    for (int i = 0; i < TCP_SERVER_HB_MAX_CLIENTS; i++) {
        if (g_hb_server.clients[i].state == TCP_SERVER_HB_CLIENT_CONNECTED) {
            tcp_server_hb_disconnect_client(i);
        }
    }

    // 关闭服务器套接字
    if (g_hb_server.server_socket_fd >= 0) {
        close(g_hb_server.server_socket_fd);
        g_hb_server.server_socket_fd = -1;
    }

    // 等待任务结束
    if (g_hb_server.server_task_handle) {
        uint32_t wait_count = 0;
        while (eTaskGetState(g_hb_server.server_task_handle) != eDeleted && wait_count < 50) {
            vTaskDelay(pdMS_TO_TICKS(100));
            wait_count++;
        }
        g_hb_server.server_task_handle = NULL;
    }

    tcp_server_hb_set_state(TCP_SERVER_HB_STATE_STOPPED);
    ESP_LOGI(TAG, "心跳服务器已停止");
}

void tcp_server_hb_destroy(void) {
    tcp_server_hb_stop();

    memset(&g_hb_server, 0, sizeof(g_hb_server));
    g_hb_server.server_socket_fd = -1;

    // 重置所有客户端套接字
    for (int i = 0; i < TCP_SERVER_HB_MAX_CLIENTS; i++) {
        g_hb_server.clients[i].socket_fd = -1;
    }

    g_hb_server_initialized = false;
    ESP_LOGI(TAG, "心跳服务器已销毁");
}

tcp_server_hb_state_t tcp_server_hb_get_state(void) { return g_hb_server.state; }

const tcp_server_hb_stats_t* tcp_server_hb_get_stats(void) { return &g_hb_server.stats; }

const tcp_server_hb_client_info_t* tcp_server_hb_get_client_info(uint32_t client_index) {
    if (client_index >= TCP_SERVER_HB_MAX_CLIENTS) {
        return NULL;
    }
    return &g_hb_server.clients[client_index];
}

void tcp_server_hb_reset_stats(void) { memset(&g_hb_server.stats, 0, sizeof(g_hb_server.stats)); }

void tcp_server_hb_print_status(void) {
    ESP_LOGI(TAG, "=== TCP心跳服务器状态 ===");
    ESP_LOGI(TAG, "状态: %d", g_hb_server.state);
    ESP_LOGI(TAG, "端口: %d", g_hb_server.config.server_port);
    ESP_LOGI(TAG, "活跃客户端: %d/%d", g_hb_server.stats.active_clients,
             g_hb_server.config.max_clients);
    ESP_LOGI(TAG, "总连接数: %d", g_hb_server.stats.total_connections);
    ESP_LOGI(TAG, "心跳包接收: %d", g_hb_server.stats.heartbeat_received_count);
    ESP_LOGI(TAG, "心跳包失败: %d", g_hb_server.stats.heartbeat_failed_count);

    for (int i = 0; i < TCP_SERVER_HB_MAX_CLIENTS; i++) {
        if (g_hb_server.clients[i].state == TCP_SERVER_HB_CLIENT_CONNECTED) {
            ESP_LOGI(TAG, "客户端 %d: 连接中, 最后心跳: %u ms ago", i,
                     tcp_server_hb_get_timestamp_ms() - g_hb_server.clients[i].last_heartbeat_time);
        }
    }
}

// 通过心跳服务器广播图传配置命令
int tcp_server_hb_send_image_config(const uint8_t* config_data, size_t config_len) {
    if (!config_data || config_len == 0) {
        ESP_LOGE(TAG, "配置数据参数无效");
        return -1;
    }

    // 创建图传配置命令帧
    uint8_t command_buffer[256];
    size_t command_len = telemetry_protocol_create_image_command(command_buffer, sizeof(command_buffer),
                                                               IMAGE_CMD_ID_JPEG_QUALITY, config_data, config_len);
    
    if (command_len == 0) {
        ESP_LOGE(TAG, "创建图传配置命令帧失败");
        return -1;
    }

    // 通过心跳服务器广播给所有连接的客户端
    uint32_t sent_count = 0;
    for (int i = 0; i < TCP_SERVER_HB_MAX_CLIENTS; i++) {
        if (g_hb_server.clients[i].state == TCP_SERVER_HB_CLIENT_CONNECTED) {
            int sent = send(g_hb_server.clients[i].socket_fd, command_buffer, command_len, 0);
            if (sent == command_len) {
                sent_count++;
                ESP_LOGI(TAG, "图传配置命令已发送给客户端 %d", i);
            } else {
                ESP_LOGW(TAG, "向客户端 %d 发送图传配置命令失败: %s", i, strerror(errno));
            }
        }
    }

    if (sent_count == 0) {
        ESP_LOGW(TAG, "没有活跃的客户端连接，无法发送图传配置命令");
        return -1; 
    }
    
    // 返回连接成功的客户端数量
    return sent_count;
}

uint32_t tcp_server_hb_get_active_client_count(void) { return g_hb_server.stats.active_clients; }
