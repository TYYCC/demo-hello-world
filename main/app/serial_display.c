/**
 * @file serial_display.c
 * @brief 屏幕显示模块 - 通过WiFi TCP接收数据并更新UI显示（无串口发送）
 * @author Your Name
 * @date 2024
 */
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "nvs_flash.h"
#include <lwip/netdb.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "serial_display.h"
#include "ui_serial_display.h"

static const char* TAG = "SERIAL_DISPLAY";

// TCP服务器配置
#define LISTEN_SOCKET_NUM 1
#define TCP_RECV_BUF_SIZE 1024
#define MAX_DISPLAY_DATA_SIZE 4096

// 任务句柄
static TaskHandle_t s_tcp_server_task_handle = NULL;
static bool s_server_running = false;
static bool s_stopping = false;

// 数据缓冲区 - 使用PSRAM
static uint8_t* s_display_buffer = NULL;
static int s_buffer_size = 0;
static SemaphoreHandle_t s_buffer_mutex = NULL;
static bool s_buffer_initialized = false;

// 初始化PSRAM缓冲区
static esp_err_t init_psram_buffer(void) {
    if (s_buffer_initialized) {
        return ESP_OK;
    }

    s_display_buffer = (uint8_t*)heap_caps_malloc(MAX_DISPLAY_DATA_SIZE, MALLOC_CAP_SPIRAM);
    if (s_display_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate PSRAM buffer for display data");
        return ESP_ERR_NO_MEM;
    }

    memset(s_display_buffer, 0, MAX_DISPLAY_DATA_SIZE);
    s_buffer_size = 0;
    s_buffer_initialized = true;

    ESP_LOGI(TAG, "PSRAM display buffer initialized: %d bytes", MAX_DISPLAY_DATA_SIZE);
    return ESP_OK;
}

// 清理PSRAM缓冲区
static void cleanup_psram_buffer(void) {
    if (s_display_buffer != NULL) {
        heap_caps_free(s_display_buffer);
        s_display_buffer = NULL;
    }
    s_buffer_initialized = false;
    s_buffer_size = 0;
}

// TCP服务器任务
static void tcp_server_task(void* pvParameters) {
    uint16_t port = *(uint16_t*)pvParameters;
    char addr_str[128];
    int addr_family = AF_INET;
    int ip_protocol = IPPROTO_IP;
    struct sockaddr_in dest_addr;

    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        s_server_running = false;
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Socket created");

    int err = bind(listen_sock, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", port);

    err = listen(listen_sock, LISTEN_SOCKET_NUM);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket listening on port %d", port);

    s_server_running = true;

    while (s_server_running) {
        struct sockaddr_in source_addr;
        socklen_t addr_len = sizeof(source_addr);

        fd_set readfds;
        struct timeval tv;
        FD_ZERO(&readfds);
        FD_SET(listen_sock, &readfds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int select_result = select(listen_sock + 1, &readfds, NULL, NULL, &tv);
        if (select_result < 0) {
            ESP_LOGE(TAG, "Select error: errno %d", errno);
            continue;
        } else if (select_result == 0) {
            continue;
        }

        int sock = accept(listen_sock, (struct sockaddr*)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            continue;
        }

        inet_ntoa_r(source_addr.sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
        ESP_LOGI(TAG, "Socket accepted IP address: %s", addr_str);

        int len;
        uint8_t rx_buffer[TCP_RECV_BUF_SIZE];

        do {
            if (!s_server_running) break;

            len = recv(sock, rx_buffer, sizeof(rx_buffer), 0);
            if (len < 0) {
                ESP_LOGE(TAG, "Error during receive: errno %d", errno);
                break;
            } else if (len == 0) {
                ESP_LOGW(TAG, "Connection closed");
                break;
            } else {
                ESP_LOGI(TAG, "Received %d bytes from TCP", len);

                if (xSemaphoreTake(s_buffer_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    if (s_buffer_initialized && s_display_buffer != NULL && len <= MAX_DISPLAY_DATA_SIZE) {
                        memcpy(s_display_buffer, rx_buffer, len);
                        s_buffer_size = len;
                        ESP_LOGI(TAG, "Data buffered for UI display");
                    } else {
                        ESP_LOGE(TAG, "Buffer not ready or data too large: %d", len);
                    }
                    xSemaphoreGive(s_buffer_mutex);
                }

                // 更新UI显示
                ui_serial_display_add_data((const char*)rx_buffer, len);
            }
        } while (s_server_running);

        shutdown(sock, 0);
        close(sock);
    }

CLEAN_UP:
    close(listen_sock);
    s_server_running = false;
    free(pvParameters);
    vTaskDelete(NULL);
}

// 公共API
esp_err_t serial_display_init(void) {
    esp_err_t ret = init_psram_buffer();
    if (ret != ESP_OK) {
        return ret;
    }

    s_buffer_mutex = xSemaphoreCreateMutex();
    if (s_buffer_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        cleanup_psram_buffer();
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Serial display module initialized (no UART)");
    return ESP_OK;
}

bool serial_display_start(uint16_t port) {
    if (s_server_running) {
        ESP_LOGW(TAG, "TCP server already running");
        return true;
    }

    uint16_t* port_param = malloc(sizeof(uint16_t));
    if (port_param == NULL) {
        ESP_LOGE(TAG, "Failed to allocate port parameter");
        return false;
    }
    *port_param = port;

    if (xTaskCreatePinnedToCore(tcp_server_task, "tcp_server", 4096, port_param, 4, &s_tcp_server_task_handle, 1) !=
        pdPASS) {
        ESP_LOGE(TAG, "Failed to create TCP server task");
        free(port_param);
        return false;
    }

    ESP_LOGI(TAG, "Serial display started on port %d", port);
    return true;
}

void serial_display_stop(void) {
    if (s_stopping) {
        ESP_LOGW(TAG, "Stop already in progress");
        return;
    }

    s_stopping = true;

    if (s_server_running) {
        s_server_running = false;
        vTaskDelay(pdMS_TO_TICKS(200));
        if (s_tcp_server_task_handle != NULL) {
            if (eTaskGetState(s_tcp_server_task_handle) != eDeleted) {
                vTaskDelete(s_tcp_server_task_handle);
            }
            s_tcp_server_task_handle = NULL;
        }
    }

    cleanup_psram_buffer();
    s_stopping = false;

    ESP_LOGI(TAG, "Serial display stopped");
}

esp_err_t serial_display_send_text(const char* text) {
    if (text == NULL) return ESP_ERR_INVALID_ARG;
    if (!s_buffer_initialized || s_display_buffer == NULL) return ESP_ERR_INVALID_STATE;

    size_t len = strlen(text);
    if (len > MAX_DISPLAY_DATA_SIZE) return ESP_ERR_INVALID_SIZE;

    if (xSemaphoreTake(s_buffer_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(s_display_buffer, text, len);
        s_buffer_size = len;
        xSemaphoreGive(s_buffer_mutex);
        ESP_LOGI(TAG, "Text buffered for UI display: %s", text);
        ui_serial_display_add_data(text, len);
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t serial_display_send_data(const uint8_t* data, size_t len) {
    if (data == NULL || len == 0) return ESP_ERR_INVALID_ARG;
    if (!s_buffer_initialized || s_display_buffer == NULL) return ESP_ERR_INVALID_STATE;
    if (len > MAX_DISPLAY_DATA_SIZE) return ESP_ERR_INVALID_SIZE;

    if (xSemaphoreTake(s_buffer_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(s_display_buffer, data, len);
        s_buffer_size = len;
        xSemaphoreGive(s_buffer_mutex);
        ESP_LOGI(TAG, "Data buffered for UI display: %d bytes", len);
        ui_serial_display_add_data((const char*)data, len);
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

bool serial_display_is_running(void) {
    return s_server_running;
}
