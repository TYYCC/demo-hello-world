/**
 * @file tcp_command_server.c
 * @brief TCP命令服务器实现 - 支持命令传输、解析和ACK应答
 * @author TidyCraze
 * @date 2025-01-27
 */

#include "tcp_command_server.h"
#include "telemetry_protocol.h"
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

static const char* TAG = "TCP_CMD_SERVER";

// 服务器管理器结构体
typedef struct {
    tcp_cmd_server_config_t config;
    tcp_cmd_server_state_t state;
    tcp_cmd_server_stats_t stats;
    tcp_cmd_client_info_t clients[TCP_CMD_SERVER_MAX_CLIENTS];
    int server_socket_fd;
    TaskHandle_t server_task_handle;
    tcp_cmd_handler_t command_handler;
    tcp_cmd_connection_callback_t connection_callback;
    bool is_initialized;
    bool is_running;
} tcp_cmd_server_manager_t;

// 全局变量
static tcp_cmd_server_manager_t g_cmd_server = {0};
static bool g_cmd_server_initialized = false;

// 内部函数声明
static bool tcp_cmd_server_bind_and_listen(void);
static void tcp_cmd_server_disconnect_client(uint32_t client_index);
static void tcp_cmd_server_handle_client_data(uint32_t client_index);
static bool tcp_cmd_server_accept_new_connection(void);
static void tcp_cmd_server_set_state(tcp_cmd_server_state_t new_state);
static uint32_t tcp_cmd_server_get_timestamp_ms(void);
static int tcp_cmd_server_find_free_client_slot(void);
static void tcp_cmd_server_cleanup_disconnected_clients(void);
static bool tcp_cmd_server_send_ack(uint32_t client_index, uint8_t original_frame_type, 
                                   const command_result_t* result);
static size_t tcp_cmd_server_process_frame_buffer(uint32_t client_index, uint8_t* buffer, size_t buffer_len);
static void tcp_cmd_server_task(void* pvParameters);

// 内部函数实现
static uint32_t tcp_cmd_server_get_timestamp_ms(void) {
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void tcp_cmd_server_set_state(tcp_cmd_server_state_t new_state) {
    if (g_cmd_server.state != new_state) {
        tcp_cmd_server_state_t old_state = g_cmd_server.state;
        g_cmd_server.state = new_state;
        ESP_LOGI(TAG, "服务器状态变更: %d -> %d", old_state, new_state);
    }
}

static int tcp_cmd_server_find_free_client_slot(void) {
    for (int i = 0; i < TCP_CMD_SERVER_MAX_CLIENTS; i++) {
        if (g_cmd_server.clients[i].state == TCP_CMD_CLIENT_DISCONNECTED) {
            return i;
        }
    }
    return -1;
}

static void tcp_cmd_server_disconnect_client(uint32_t client_index) {
    if (client_index >= TCP_CMD_SERVER_MAX_CLIENTS) {
        return;
    }

    tcp_cmd_client_info_t* client = &g_cmd_server.clients[client_index];

    if (client->socket_fd >= 0) {
        close(client->socket_fd);
        client->socket_fd = -1;
        ESP_LOGI(TAG, "客户端 %d 已断开", client_index);
    }

    client->state = TCP_CMD_CLIENT_DISCONNECTED;
    client->recv_buffer_len = 0;

    // 调用连接状态变更回调
    if (g_cmd_server.connection_callback) {
        g_cmd_server.connection_callback(client_index, false);
    }

    g_cmd_server.stats.active_clients--;
}

static bool tcp_cmd_server_bind_and_listen(void) {
    // 创建服务器套接字
    g_cmd_server.server_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_cmd_server.server_socket_fd < 0) {
        ESP_LOGE(TAG, "创建服务器套接字失败: %s", strerror(errno));
        return false;
    }

    // 设置套接字选项
    int opt = 1;
    if (setsockopt(g_cmd_server.server_socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        ESP_LOGE(TAG, "设置SO_REUSEADDR失败: %s", strerror(errno));
        close(g_cmd_server.server_socket_fd);
        g_cmd_server.server_socket_fd = -1;
        return false;
    }

    // 绑定地址和端口
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(g_cmd_server.config.server_port);

    if (bind(g_cmd_server.server_socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "绑定端口 %d 失败: %s", g_cmd_server.config.server_port, strerror(errno));
        close(g_cmd_server.server_socket_fd);
        g_cmd_server.server_socket_fd = -1;
        return false;
    }

    // 开始监听
    if (listen(g_cmd_server.server_socket_fd, 5) < 0) {
        ESP_LOGE(TAG, "监听端口 %d 失败: %s", g_cmd_server.config.server_port, strerror(errno));
        close(g_cmd_server.server_socket_fd);
        g_cmd_server.server_socket_fd = -1;
        return false;
    }

    ESP_LOGI(TAG, "命令服务器开始监听端口 %d", g_cmd_server.config.server_port);
    return true;
}

static bool tcp_cmd_server_accept_new_connection(void) {
    if (g_cmd_server.stats.active_clients >= g_cmd_server.config.max_clients) {
        ESP_LOGW(TAG, "已达到最大客户端连接数 (%d)，拒绝新连接", g_cmd_server.config.max_clients);
        return false;
    }

    int client_slot = tcp_cmd_server_find_free_client_slot();
    if (client_slot < 0) {
        ESP_LOGW(TAG, "没有可用的客户端槽位");
        return false;
    }

    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    int client_fd = accept(g_cmd_server.server_socket_fd, (struct sockaddr*)&client_addr, &client_addr_len);
    if (client_fd < 0) {
        ESP_LOGE(TAG, "接受连接失败: %s", strerror(errno));
        return false;
    }

    // 设置客户端套接字为非阻塞模式
    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

    // 设置接收超时
    struct timeval timeout;
    timeout.tv_sec = g_cmd_server.config.recv_timeout_ms / 1000;
    timeout.tv_usec = (g_cmd_server.config.recv_timeout_ms % 1000) * 1000;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    // 初始化客户端信息
    tcp_cmd_client_info_t* client = &g_cmd_server.clients[client_slot];
    client->socket_fd = client_fd;
    client->state = TCP_CMD_CLIENT_CONNECTED;
    client->connection_time = tcp_cmd_server_get_timestamp_ms();
    client->last_activity_time = client->connection_time;
    client->recv_buffer_len = 0;

    g_cmd_server.stats.total_connections++;
    g_cmd_server.stats.active_clients++;

    ESP_LOGI(TAG, "新客户端连接: 槽位=%d, 套接字=%d, 地址=%s:%d", client_slot, client_fd,
             inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

    // 调用连接状态变更回调
    if (g_cmd_server.connection_callback) {
        g_cmd_server.connection_callback(client_slot, true);
    }

    return true;
}

static bool tcp_cmd_server_send_ack(uint32_t client_index, uint8_t original_frame_type, 
                                   const command_result_t* result) {
    if (client_index >= TCP_CMD_SERVER_MAX_CLIENTS || !result) {
        return false;
    }

    tcp_cmd_client_info_t* client = &g_cmd_server.clients[client_index];
    if (client->state != TCP_CMD_CLIENT_CONNECTED) {
        return false;
    }

    uint8_t ack_buffer[128];
    size_t ack_len = tcp_command_server_create_ack_frame(ack_buffer, sizeof(ack_buffer),
                                                        original_frame_type, result->status,
                                                        result->response_data, result->response_len);
    
    if (ack_len == 0) {
        ESP_LOGE(TAG, "创建ACK帧失败");
        return false;
    }

    int sent = send(client->socket_fd, ack_buffer, ack_len, 0);
    if (sent != ack_len) {
        ESP_LOGW(TAG, "发送ACK失败: %s", strerror(errno));
        return false;
    }

    g_cmd_server.stats.ack_sent++;
    ESP_LOGD(TAG, "向客户端 %d 发送ACK: 类型=0x%02X, 状态=%d", client_index, original_frame_type, result->status);
    return true;
}

static size_t tcp_cmd_server_process_frame_buffer(uint32_t client_index, uint8_t* buffer, size_t buffer_len) {
    parsed_frame_t parsed_frame;
    size_t frame_len = telemetry_protocol_parse_frame(buffer, buffer_len, &parsed_frame);
    
    if (frame_len == 0) {
        return 0; // 帧不完整或解析失败
    }

    if (!parsed_frame.crc_ok) {
        ESP_LOGW(TAG, "客户端 %d CRC校验失败", client_index);
        g_cmd_server.stats.errors++;
        
        // 发送错误ACK
        command_result_t error_result = {
            .status = ACK_STATUS_ERROR,
            .response_len = 0
        };
        tcp_cmd_server_send_ack(client_index, parsed_frame.header.type, &error_result);
        return frame_len;
    }

    g_cmd_server.stats.commands_received++;
    
    // 调用命令处理回调
    command_result_t result = {0};
    if (g_cmd_server.command_handler) {
        result = g_cmd_server.command_handler(parsed_frame.header.type, parsed_frame.payload, 
                                            parsed_frame.payload_len, client_index);
        g_cmd_server.stats.commands_processed++;
    } else {
        result.status = ACK_STATUS_ERROR;
        result.response_len = 0;
    }

    // 发送ACK应答
    tcp_cmd_server_send_ack(client_index, parsed_frame.header.type, &result);
    
    return frame_len;
}

static void tcp_cmd_server_handle_client_data(uint32_t client_index) {
    if (client_index >= TCP_CMD_SERVER_MAX_CLIENTS) {
        return;
    }

    tcp_cmd_client_info_t* client = &g_cmd_server.clients[client_index];
    if (client->state != TCP_CMD_CLIENT_CONNECTED) {
        return;
    }

    // 接收数据到缓冲区
    uint8_t temp_buffer[256];
    int recv_len = recv(client->socket_fd, temp_buffer, sizeof(temp_buffer), 0);

    if (recv_len == 0) {
        // 客户端主动断开连接
        ESP_LOGI(TAG, "客户端 %d 主动断开连接", client_index);
        tcp_cmd_server_disconnect_client(client_index);
        return;
    } else if (recv_len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 没有数据可读，这是正常的
            return;
        } else {
            // 发生错误
            ESP_LOGW(TAG, "客户端 %d 接收数据错误: %s", client_index, strerror(errno));
            tcp_cmd_server_disconnect_client(client_index);
            return;
        }
    }

    // 将接收到的数据添加到客户端缓冲区
    if (client->recv_buffer_len + recv_len > TCP_CMD_SERVER_BUFFER_SIZE) {
        ESP_LOGW(TAG, "客户端 %d 接收缓冲区溢出，清空缓冲区", client_index);
        client->recv_buffer_len = 0;
        g_cmd_server.stats.errors++;
        return;
    }

    memcpy(&client->recv_buffer[client->recv_buffer_len], temp_buffer, recv_len);
    client->recv_buffer_len += recv_len;
    client->last_activity_time = tcp_cmd_server_get_timestamp_ms();

    // 处理缓冲区中的完整帧
    size_t processed = 0;
    while (processed < client->recv_buffer_len) {
        size_t frame_len = tcp_cmd_server_process_frame_buffer(client_index, 
                                                              &client->recv_buffer[processed], 
                                                              client->recv_buffer_len - processed);
        if (frame_len == 0) {
            break; // 没有完整的帧
        }
        processed += frame_len;
    }

    // 移除已处理的数据
    if (processed > 0) {
        if (processed < client->recv_buffer_len) {
            memmove(client->recv_buffer, &client->recv_buffer[processed], 
                   client->recv_buffer_len - processed);
        }
        client->recv_buffer_len -= processed;
    }
}

static void tcp_cmd_server_cleanup_disconnected_clients(void) {
    uint32_t current_time = tcp_cmd_server_get_timestamp_ms();
    
    for (int i = 0; i < TCP_CMD_SERVER_MAX_CLIENTS; i++) {
        tcp_cmd_client_info_t* client = &g_cmd_server.clients[i];
        
        if (client->state == TCP_CMD_CLIENT_CONNECTED) {
            // 检查活动超时（可选功能）
            if (current_time - client->last_activity_time > 300000) { // 5分钟超时
                ESP_LOGW(TAG, "客户端 %d 活动超时，断开连接", i);
                tcp_cmd_server_disconnect_client(i);
            }
        }
    }
}

static void tcp_cmd_server_task(void* pvParameters) {
    (void)pvParameters;
    
    ESP_LOGI(TAG, "命令服务器任务启动");
    
    fd_set read_fds;
    struct timeval timeout;
    
    while (g_cmd_server.is_running) {
        // 清理断开的客户端
        tcp_cmd_server_cleanup_disconnected_clients();
        
        // 准备select
        FD_ZERO(&read_fds);
        int max_fd = g_cmd_server.server_socket_fd;
        
        // 添加服务器套接字
        FD_SET(g_cmd_server.server_socket_fd, &read_fds);
        
        // 添加所有客户端套接字
        for (int i = 0; i < TCP_CMD_SERVER_MAX_CLIENTS; i++) {
            if (g_cmd_server.clients[i].state == TCP_CMD_CLIENT_CONNECTED) {
                FD_SET(g_cmd_server.clients[i].socket_fd, &read_fds);
                if (g_cmd_server.clients[i].socket_fd > max_fd) {
                    max_fd = g_cmd_server.clients[i].socket_fd;
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
        if (FD_ISSET(g_cmd_server.server_socket_fd, &read_fds)) {
            tcp_cmd_server_accept_new_connection();
        }
        
        // 检查客户端数据
        for (int i = 0; i < TCP_CMD_SERVER_MAX_CLIENTS; i++) {
            if (g_cmd_server.clients[i].state == TCP_CMD_CLIENT_CONNECTED &&
                FD_ISSET(g_cmd_server.clients[i].socket_fd, &read_fds)) {
                tcp_cmd_server_handle_client_data(i);
            }
        }
    }
    
    ESP_LOGI(TAG, "命令服务器任务结束");
    g_cmd_server.server_task_handle = NULL;
    vTaskDelete(NULL);
}

// 公共接口实现
bool tcp_command_server_init(const tcp_cmd_server_config_t* config) {
    if (g_cmd_server_initialized) {
        ESP_LOGW(TAG, "命令服务器已初始化");
        return true;
    }

    // 初始化配置
    memset(&g_cmd_server, 0, sizeof(g_cmd_server));

    if (config != NULL) {
        g_cmd_server.config = *config;
    } else {
        // 使用默认配置
        g_cmd_server.config.server_port = TCP_CMD_SERVER_DEFAULT_PORT;
        g_cmd_server.config.max_clients = TCP_CMD_SERVER_MAX_CLIENTS;
        g_cmd_server.config.ack_timeout_ms = TCP_CMD_SERVER_ACK_TIMEOUT_MS;
        g_cmd_server.config.recv_timeout_ms = TCP_CMD_SERVER_RECV_TIMEOUT_MS;
    }

    // 初始化客户端数组
    for (int i = 0; i < TCP_CMD_SERVER_MAX_CLIENTS; i++) {
        g_cmd_server.clients[i].socket_fd = -1;
        g_cmd_server.clients[i].state = TCP_CMD_CLIENT_DISCONNECTED;
        g_cmd_server.clients[i].recv_buffer_len = 0;
    }

    g_cmd_server.server_socket_fd = -1;
    g_cmd_server.state = TCP_CMD_SERVER_STATE_STOPPED;
    g_cmd_server.is_initialized = true;
    g_cmd_server.is_running = false;

    g_cmd_server_initialized = true;
    ESP_LOGI(TAG, "命令服务器初始化成功，端口: %d", g_cmd_server.config.server_port);

    return true;
}

bool tcp_command_server_start(const char* task_name, uint32_t stack_size, UBaseType_t task_priority,
                             tcp_cmd_handler_t cmd_handler, tcp_cmd_connection_callback_t conn_callback) {
    if (!g_cmd_server_initialized) {
        ESP_LOGE(TAG, "命令服务器未初始化");
        return false;
    }

    if (g_cmd_server.is_running) {
        ESP_LOGW(TAG, "命令服务器已在运行");
        return true;
    }

    if (!cmd_handler) {
        ESP_LOGE(TAG, "命令处理回调函数不能为空");
        return false;
    }

    g_cmd_server.command_handler = cmd_handler;
    g_cmd_server.connection_callback = conn_callback;

    // 绑定并监听端口
    if (!tcp_cmd_server_bind_and_listen()) {
        ESP_LOGE(TAG, "绑定和监听失败");
        return false;
    }

    // 设置默认参数
    const char* name = task_name ? task_name : "tcp_cmd_server";
    uint32_t stack = (stack_size > 0) ? stack_size : 6144;

    // 创建任务
    BaseType_t result = xTaskCreate(tcp_cmd_server_task, name, stack, NULL, task_priority,
                                   &g_cmd_server.server_task_handle);

    if (result != pdPASS) {
        ESP_LOGE(TAG, "创建命令服务器任务失败");
        close(g_cmd_server.server_socket_fd);
        g_cmd_server.server_socket_fd = -1;
        return false;
    }

    g_cmd_server.is_running = true;
    tcp_cmd_server_set_state(TCP_CMD_SERVER_STATE_RUNNING);

    ESP_LOGI(TAG, "命令服务器启动成功，监听端口: %d", g_cmd_server.config.server_port);
    return true;
}

void tcp_command_server_stop(void) {
    if (!g_cmd_server.is_running) {
        return;
    }

    g_cmd_server.is_running = false;

    // 断开所有客户端连接
    for (int i = 0; i < TCP_CMD_SERVER_MAX_CLIENTS; i++) {
        if (g_cmd_server.clients[i].state == TCP_CMD_CLIENT_CONNECTED) {
            tcp_cmd_server_disconnect_client(i);
        }
    }

    // 关闭服务器套接字
    if (g_cmd_server.server_socket_fd >= 0) {
        close(g_cmd_server.server_socket_fd);
        g_cmd_server.server_socket_fd = -1;
    }

    // 等待任务结束
    if (g_cmd_server.server_task_handle) {
        uint32_t wait_count = 0;
        while (eTaskGetState(g_cmd_server.server_task_handle) != eDeleted && wait_count < 50) {
            vTaskDelay(pdMS_TO_TICKS(100));
            wait_count++;
        }
        g_cmd_server.server_task_handle = NULL;
    }

    tcp_cmd_server_set_state(TCP_CMD_SERVER_STATE_STOPPED);
    ESP_LOGI(TAG, "命令服务器已停止");
}

void tcp_command_server_destroy(void) {
    tcp_command_server_stop();

    memset(&g_cmd_server, 0, sizeof(g_cmd_server));
    g_cmd_server.server_socket_fd = -1;

    // 重置所有客户端套接字
    for (int i = 0; i < TCP_CMD_SERVER_MAX_CLIENTS; i++) {
        g_cmd_server.clients[i].socket_fd = -1;
    }

    g_cmd_server_initialized = false;
    ESP_LOGI(TAG, "命令服务器已销毁");
}

tcp_cmd_server_state_t tcp_command_server_get_state(void) {
    return g_cmd_server.state;
}

const tcp_cmd_server_stats_t* tcp_command_server_get_stats(void) {
    return &g_cmd_server.stats;
}

const tcp_cmd_client_info_t* tcp_command_server_get_client_info(uint32_t client_index) {
    if (client_index >= TCP_CMD_SERVER_MAX_CLIENTS) {
        return NULL;
    }
    return &g_cmd_server.clients[client_index];
}

void tcp_command_server_reset_stats(void) {
    memset(&g_cmd_server.stats, 0, sizeof(g_cmd_server.stats));
}

void tcp_command_server_print_status(void) {
    ESP_LOGI(TAG, "=== TCP命令服务器状态 ===");
    ESP_LOGI(TAG, "状态: %d", g_cmd_server.state);
    ESP_LOGI(TAG, "端口: %d", g_cmd_server.config.server_port);
    ESP_LOGI(TAG, "活跃客户端: %d/%d", g_cmd_server.stats.active_clients, g_cmd_server.config.max_clients);
    ESP_LOGI(TAG, "总连接数: %d", g_cmd_server.stats.total_connections);
    ESP_LOGI(TAG, "命令接收: %d", g_cmd_server.stats.commands_received);
    ESP_LOGI(TAG, "命令处理: %d", g_cmd_server.stats.commands_processed);
    ESP_LOGI(TAG, "ACK发送: %d", g_cmd_server.stats.ack_sent);
    ESP_LOGI(TAG, "错误数: %d", g_cmd_server.stats.errors);

    for (int i = 0; i < TCP_CMD_SERVER_MAX_CLIENTS; i++) {
        if (g_cmd_server.clients[i].state == TCP_CMD_CLIENT_CONNECTED) {
            ESP_LOGI(TAG, "客户端 %d: 连接中, 最后活动: %u ms ago", i,
                     tcp_cmd_server_get_timestamp_ms() - g_cmd_server.clients[i].last_activity_time);
        }
    }
}

uint32_t tcp_command_server_get_active_client_count(void) {
    return g_cmd_server.stats.active_clients;
}

bool tcp_command_server_send_to_client(uint32_t client_index, uint8_t frame_type, 
                                      const uint8_t* payload, size_t payload_len) {
    if (client_index >= TCP_CMD_SERVER_MAX_CLIENTS) {
        return false;
    }

    tcp_cmd_client_info_t* client = &g_cmd_server.clients[client_index];
    if (client->state != TCP_CMD_CLIENT_CONNECTED) {
        return false;
    }

    uint8_t frame_buffer[512];
    size_t frame_len = 0;

    // 根据帧类型创建相应的帧
    switch (frame_type) {
        case FRAME_TYPE_TELEMETRY:
            // 这里应该调用相应的协议创建函数
            // frame_len = telemetry_protocol_create_telemetry_frame(...);
            break;
        case FRAME_TYPE_EXT_CMD:
            if (payload_len >= 2) {
                frame_len = telemetry_protocol_create_ext_command(frame_buffer, sizeof(frame_buffer),
                                                                 payload[0], &payload[2], payload[1]);
            }
            break;
        default:
            ESP_LOGW(TAG, "不支持的帧类型: 0x%02X", frame_type);
            return false;
    }

    if (frame_len == 0) {
        ESP_LOGE(TAG, "创建帧失败");
        return false;
    }

    int sent = send(client->socket_fd, frame_buffer, frame_len, 0);
    if (sent != frame_len) {
        ESP_LOGW(TAG, "发送数据失败: %s", strerror(errno));
        return false;
    }

    return true;
}

uint32_t tcp_command_server_broadcast(uint8_t frame_type, const uint8_t* payload, size_t payload_len) {
    uint32_t success_count = 0;
    
    for (int i = 0; i < TCP_CMD_SERVER_MAX_CLIENTS; i++) {
        if (tcp_command_server_send_to_client(i, frame_type, payload, payload_len)) {
            success_count++;
        }
    }
    
    return success_count;
}

size_t tcp_command_server_create_ack_frame(uint8_t* buffer, size_t buffer_size,
                                          uint8_t original_frame_type, ack_status_t ack_status,
                                          const uint8_t* response_data, size_t response_len) {
    if (!buffer || buffer_size < 7) { // 最小ACK帧长度
        return 0;
    }

    // 限制响应数据长度
    if (response_len > 64) {
        response_len = 64;
    }

    // 计算负载长度
    size_t payload_len = 3 + response_len; // original_frame_type + ack_status + data_len + data
    
    // 检查缓冲区大小
    size_t total_frame_len = 2 + 1 + 1 + payload_len + 2; // header + len + type + payload + crc
    if (buffer_size < total_frame_len) {
        return 0;
    }

    // 构建负载
    uint8_t payload[67]; // 3 + 64
    payload[0] = original_frame_type;
    payload[1] = (uint8_t)ack_status;
    payload[2] = (uint8_t)response_len;
    if (response_data && response_len > 0) {
        memcpy(&payload[3], response_data, response_len);
    }

    // 使用协议函数创建帧（需要扩展协议以支持ACK帧类型）
    // 这里手动构建ACK帧
    buffer[0] = FRAME_HEADER_1;
    buffer[1] = FRAME_HEADER_2;
    buffer[2] = 1 + payload_len; // length field
    buffer[3] = FRAME_TYPE_ACK;
    
    // 复制负载
    memcpy(&buffer[4], payload, payload_len);
    
    // 计算CRC (需要包含协议中的CRC计算函数)
    extern uint16_t crc16_modbus_table(const uint8_t* data, uint16_t length);
    uint16_t crc = crc16_modbus_table(&buffer[2], 1 + 1 + payload_len);
    
    size_t crc_offset = 4 + payload_len;
    buffer[crc_offset] = crc & 0xFF;
    buffer[crc_offset + 1] = (crc >> 8) & 0xFF;
    
    return total_frame_len;
}