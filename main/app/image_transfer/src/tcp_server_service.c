#include "tcp_server_service.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <fcntl.h>
#include <unistd.h>
#include "image_transfer_protocol.h"
#include "raw_data_service.h"
#include "lz4_decoder_service.h"
#include "jpeg_decoder_service.h"
#include "image_transfer_app.h"

static const char* TAG = "TCP_SERVER_SERVICE";

// 全局状态变量
static bool s_tcp_server_running = false;
static int s_tcp_server_socket = -1;
static TaskHandle_t s_tcp_server_task = NULL;
static EventGroupHandle_t s_tcp_event_group = NULL;

#define TCP_SERVER_STOP_BIT (1 << 0)
#define TCP_SERVER_CONNECTED_BIT (1 << 1)

// TCP服务器任务函数
static void tcp_server_task(void* pvParameters) {
    int client_socket = -1;
    struct sockaddr_in6 client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    uint8_t* recv_buffer = malloc(100 * 1024);  // 增加缓冲区到100KB
    if (!recv_buffer) {
        ESP_LOGE(TAG, "Failed to allocate recv_buffer");
        vTaskDelete(NULL);
        return;
    }

    while (s_tcp_server_running) {
        client_socket = accept(s_tcp_server_socket, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_socket < 0) {
            if (errno != EWOULDBLOCK && errno != EAGAIN) {
                ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        ESP_LOGI(TAG, "Client connected");
        xEventGroupSetBits(s_tcp_event_group, TCP_SERVER_CONNECTED_BIT);

        size_t buffer_offset = 0;
        while (s_tcp_server_running) {
            int len = recv(client_socket, recv_buffer + buffer_offset, (100 * 1024) - buffer_offset, 0);
            if (len < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    ESP_LOGE(TAG, "Recv failed: errno %d", errno);
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            } else if (len == 0) {
                ESP_LOGI(TAG, "Client disconnected");
                break;
            }

            buffer_offset += len;

            // 检查缓冲区是否已满
            if (buffer_offset >= (100 * 1024)) {
                ESP_LOGW(TAG, "Receive buffer full (%d bytes), possible data loss", buffer_offset);
                // 可以选择清空缓冲区或增加处理速度
                buffer_offset = 0;
            }
            size_t consumed = 0;

            while (consumed < buffer_offset) {
                if (buffer_offset - consumed < sizeof(image_transfer_header_t)) {
                    break; 
                }

                image_transfer_header_t* header = (image_transfer_header_t*)(recv_buffer + consumed);

                if (header->sync_word != PROTOCOL_SYNC_WORD) {
    ESP_LOGW(TAG, "Invalid sync word (received: 0x%08X, expected: 0x%08X). Searching for sync word...", header->sync_word, PROTOCOL_SYNC_WORD);
                    
    size_t search_offset = 1;
    while (consumed + search_offset + sizeof(uint32_t) <= buffer_offset) {
        uint32_t potential_sync = *(uint32_t*)(recv_buffer + consumed + search_offset);
        if (potential_sync == PROTOCOL_SYNC_WORD) {
            consumed += search_offset;
            header = (image_transfer_header_t*)(recv_buffer + consumed);
            ESP_LOGI(TAG, "Sync word found at offset %d", consumed);
            goto sync_found;
        }
        search_offset++;
    }
                    
                    consumed = buffer_offset > 3 ? buffer_offset - 3 : 0;
                    break;
                }
            sync_found:
                if (buffer_offset - consumed < sizeof(image_transfer_header_t) + header->data_len) {
                    break; 
                }

                uint8_t* payload = (uint8_t*)(header + 1);

                // 根据数据类型自动切换解码器模式
                image_transfer_mode_t required_mode = IMAGE_TRANSFER_MODE_JPEG; // 默认
                switch (header->data_type) {
                    case DATA_TYPE_JPEG:
                        required_mode = IMAGE_TRANSFER_MODE_JPEG;
                        ESP_LOGI(TAG, "Processing JPEG data: %u bytes", header->data_len);
                        break;
                    case DATA_TYPE_LZ4:
                        required_mode = IMAGE_TRANSFER_MODE_LZ4;
                        ESP_LOGI(TAG, "Processing LZ4 data: %u bytes", header->data_len);
                        break;
                    case DATA_TYPE_RAW:
                        required_mode = IMAGE_TRANSFER_MODE_RAW;
                        ESP_LOGI(TAG, "Processing RAW data: %u bytes", header->data_len);
                        break;
                    default:
                        ESP_LOGW(TAG, "Unknown data type: 0x%02X", header->data_len);
                        break;
                }

                // 如果当前模式不是需要的模式，自动切换
                if (image_transfer_app_get_mode() != required_mode) {
                    ESP_LOGI(TAG, "Auto-switching mode from %d to %d", image_transfer_app_get_mode(), required_mode);
                    esp_err_t switch_ret = image_transfer_app_set_mode(required_mode);
                    if (switch_ret != ESP_OK) {
                        ESP_LOGW(TAG, "Failed to auto-switch mode: %d", switch_ret);
                    } else {
                        // 给解码器一些时间来完全启动
                        vTaskDelay(pdMS_TO_TICKS(10));
                    }
                }

                // 现在处理数据
                switch (header->data_type) {
                    case DATA_TYPE_JPEG:
                        if (jpeg_decoder_service_is_running()) {
                            jpeg_decoder_service_process_data(payload, header->data_len);
                        } else {
                            ESP_LOGW(TAG, "JPEG decoder not running, skipping data processing");
                        }
                        break;
                    case DATA_TYPE_LZ4:
                        if (lz4_decoder_service_is_running()) {
                            lz4_decoder_service_process_data(payload, header->data_len);
                        } else {
                            ESP_LOGW(TAG, "LZ4 decoder not running, skipping data processing");
                        }
                        break;
                    case DATA_TYPE_RAW:
                        if (raw_data_service_is_running()) {
                            raw_data_service_process_data(payload, header->data_len);
                        } else {
                            ESP_LOGW(TAG, "RAW decoder not running, skipping data processing");
                        }
                        break;
                    default:
                        // Unknown data type already logged above
                        break;
                }
                consumed += sizeof(image_transfer_header_t) + header->data_len;
            }

            if (consumed > 0 && consumed < buffer_offset) {
                memmove(recv_buffer, recv_buffer + consumed, buffer_offset - consumed);
                buffer_offset -= consumed;
            } else if (consumed == buffer_offset) {
                buffer_offset = 0;
            }
        }

        close(client_socket);
        xEventGroupClearBits(s_tcp_event_group, TCP_SERVER_CONNECTED_BIT);
    }

    free(recv_buffer);
    vTaskDelete(NULL);
}

// 初始化TCP服务器
esp_err_t tcp_server_service_init(int port) {
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
    
    // 设置TCP keepalive选项以防止连接超时
    int keepalive = 1;
    int keepidle = 30;     // 30秒后开始发送keepalive探测
    int keepintvl = 5;      // 每5秒发送一次keepalive探测
    int keepcnt = 3;        // 最多发送3次探测
    
    setsockopt(s_tcp_server_socket, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
    setsockopt(s_tcp_server_socket, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
    setsockopt(s_tcp_server_socket, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
    setsockopt(s_tcp_server_socket, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));
    
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