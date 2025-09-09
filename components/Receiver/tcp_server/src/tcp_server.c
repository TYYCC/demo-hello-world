#include "tcp_server.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi_pairing_manager.h"

#define TCP_SERVER_PORT 1100
#define TCP_SERVER_MAX_CONN 1

static const char *TAG = "tcp_server";
static int server_socket = -1;
static TaskHandle_t server_task_handle = NULL;

static void tcp_server_task(void *pvParameters) {
    char addr_str[128];
    int addr_family = AF_INET;
    int ip_protocol = 0;
    struct sockaddr_in dest_addr;

    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(TCP_SERVER_PORT);
    ip_protocol = IPPROTO_IP;

    server_socket = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (server_socket < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket created");

    int err = bind(server_socket, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", TCP_SERVER_PORT);

    err = listen(server_socket, TCP_SERVER_MAX_CONN);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }

    while (1) {
        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_in source_addr;
        socklen_t addr_len = sizeof(source_addr);
        int sock = accept(server_socket, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }

        // Convert ip address to string
        if (source_addr.sin_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
        }
        ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);

        // Handle client connection
        // TODO: Add logic to parse special commands
        char rx_buffer[128];
        int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        if (len < 0) {
            ESP_LOGE(TAG, "recv failed: errno %d", errno);
        } else {
            rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string
            ESP_LOGI(TAG, "Received %d bytes: %s", len, rx_buffer);
            // Example of command parsing
            if (strncmp(rx_buffer, "get_status", len) == 0) {
                send(sock, "OK", 2, 0);
            }
        }

        shutdown(sock, 0);
        close(sock);
    }

CLEAN_UP:
    close(server_socket);
    server_socket = -1;
    vTaskDelete(NULL);
}

esp_err_t tcp_server_start(void) {
    if (wifi_pairing_get_state() != WIFI_PAIRING_STATE_CONNECTED) {
        if (server_task_handle == NULL) {
            xTaskCreate(tcp_server_task, "tcp_server", 4096, NULL, 5, &server_task_handle);
            ESP_LOGI(TAG, "TCP server started");
        }
        return ESP_OK;
    }
    ESP_LOGI(TAG, "WiFi is connected, TCP server not started.");
    return ESP_FAIL;
}

void tcp_server_stop(void) {
    if (server_task_handle != NULL) {
        vTaskDelete(server_task_handle);
        server_task_handle = NULL;
    }
    if (server_socket != -1) {
        close(server_socket);
        server_socket = -1;
    }
    ESP_LOGI(TAG, "TCP server stopped");
}