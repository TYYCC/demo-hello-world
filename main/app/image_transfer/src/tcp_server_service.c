#include "tcp_server_service.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

static const char* TAG = "TCP_SERVER_SERVICE";

// 全局状态变量
static bool s_tcp_server_running = false;
static int s_tcp_server_socket = -1;
static TaskHandle_t s_tcp_server_task = NULL;
static EventGroupHandle_t s_tcp_event_group = NULL;
static tcp_server_callback_t s_data_callback = NULL;
static void* s_callback_context = NULL;

#define TCP_SERVER_STOP_BIT (1 << 0)
#define TCP_SERVER_CONNECTED_BIT (1 << 1)

// TCP服务器任务函数
static void tcp_server_task(void* pvParameters) {
    int client_socket = -1;
    struct sockaddr_in6 client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    
    while (s_tcp_server_running) {
        // 接受客户端连接
        client_socket = accept(s_tcp_server_socket, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_socket < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                ESP_LOGE(TAG, "Failed to accept client connection: errno %d", errno);
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        ESP_LOGI(TAG, "Client connected from %s", inet6_ntoa(client_addr.sin6_addr));
        xEventGroupSetBits(s_tcp_event_group, TCP_SERVER_CONNECTED_BIT);
        
        // 处理客户端数据
        uint8_t buffer[2048];
        while (s_tcp_server_running) {
            int len = recv(client_socket, buffer, sizeof(buffer), 0);
            if (len < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                    continue;
                }
                ESP_LOGE(TAG, "recv failed: errno %d", errno);
                break;
            } else if (len == 0) {
                ESP_LOGI(TAG, "Client disconnected");
                break;
            }
            
            // 调用数据回调函数处理接收到的数据
            if (s_data_callback) {
                s_data_callback(buffer, len, s_callback_context);
            }
        }
        
        close(client_socket);
        xEventGroupClearBits(s_tcp_event_group, TCP_SERVER_CONNECTED_BIT);
    }
    
    vTaskDelete(NULL);
}

// 初始化TCP服务器
esp_err_t tcp_server_service_init(int port, tcp_server_callback_t data_callback, void* context) {
    if (s_tcp_server_running) {
        ESP_LOGW(TAG, "TCP server already running");
        return ESP_FAIL;
    }
    
    // 创建事件组
    s_tcp_event_group = xEventGroupCreate();
    if (!s_tcp_event_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }
    
    // 创建TCP服务器socket
    s_tcp_server_socket = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (s_tcp_server_socket < 0) {
        ESP_LOGE(TAG, "Failed to create socket: errno %d", errno);
        vEventGroupDelete(s_tcp_event_group);
        return ESP_FAIL;
    }
    
    // 设置socket选项
    int opt = 1;
    setsockopt(s_tcp_server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // 绑定地址
    struct sockaddr_in6 server_addr = {
        .sin6_family = AF_INET6,
        .sin6_addr = in6addr_any,
        .sin6_port = htons(port)
    };
    
    if (bind(s_tcp_server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) != 0) {
        ESP_LOGE(TAG, "Failed to bind socket: errno %d", errno);
        close(s_tcp_server_socket);
        vEventGroupDelete(s_tcp_event_group);
        return ESP_FAIL;
    }
    
    // 开始监听
    if (listen(s_tcp_server_socket, 5) != 0) {
        ESP_LOGE(TAG, "Failed to listen on socket: errno %d", errno);
        close(s_tcp_server_socket);
        vEventGroupDelete(s_tcp_event_group);
        return ESP_FAIL;
    }
    
    // 设置非阻塞模式
    fcntl(s_tcp_server_socket, F_SETFL, O_NONBLOCK);
    
    s_data_callback = data_callback;
    s_callback_context = context;
    s_tcp_server_running = true;
    
    // 创建TCP服务器任务
    if (xTaskCreate(tcp_server_task, "tcp_server", 4096, NULL, 5, &s_tcp_server_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create TCP server task");
        close(s_tcp_server_socket);
        vEventGroupDelete(s_tcp_event_group);
        s_tcp_server_running = false;
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "TCP server started on port %d", port);
    return ESP_OK;
}

// 停止TCP服务器
void tcp_server_service_deinit(void) {
    if (!s_tcp_server_running) {
        return;
    }
    
    s_tcp_server_running = false;
    
    // 停止任务
    if (s_tcp_server_task) {
        vTaskDelete(s_tcp_server_task);
        s_tcp_server_task = NULL;
    }
    
    // 关闭socket
    if (s_tcp_server_socket >= 0) {
        close(s_tcp_server_socket);
        s_tcp_server_socket = -1;
    }
    
    // 删除事件组
    if (s_tcp_event_group) {
        vEventGroupDelete(s_tcp_event_group);
        s_tcp_event_group = NULL;
    }
    
    s_data_callback = NULL;
    s_callback_context = NULL;
    
    ESP_LOGI(TAG, "TCP server stopped");
}

// 获取TCP服务器状态
bool tcp_server_service_is_running(void) {
    return s_tcp_server_running;
}

// 获取连接状态
bool tcp_server_service_is_connected(void) {
    if (!s_tcp_event_group) return false;
    return (xEventGroupGetBits(s_tcp_event_group) & TCP_SERVER_CONNECTED_BIT) != 0;
}

// 获取事件组句柄
EventGroupHandle_t tcp_server_service_get_event_group(void) {
    return s_tcp_event_group;
}