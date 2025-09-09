#include "tcp_server.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi_pairing_manager.h"
#include "tcp_common_protocol.h"

#define TCP_SERVER_PORT 1100
#define TCP_SERVER_MAX_CONN 1

static const char *TAG = "tcp_server";
static int server_socket = -1;
static TaskHandle_t server_task_handle = NULL;

static void handle_special_command(const uint8_t *data, uint16_t len);

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

        if (source_addr.sin_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
        }
        ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);

        uint8_t rx_buffer[128];
        int len = recv(sock, rx_buffer, sizeof(rx_buffer), 0);
        if (len < 0) {
            ESP_LOGE(TAG, "recv failed: errno %d", errno);
        } else if (len > 0) {
            if (validate_frame(rx_buffer, len)) {
                uint8_t frame_type = rx_buffer[3];
                if (frame_type == FRAME_TYPE_SPECIAL_CMD) {
                    handle_special_command(rx_buffer, len);
                } else {
                    ESP_LOGW(TAG, "Received valid frame but with unexpected type: 0x%02X", frame_type);
                }
            } else {
                ESP_LOGW(TAG, "Received invalid frame");
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

static void handle_special_command(const uint8_t *data, uint16_t len) {
    const uint8_t *payload = data + 4;
    uint16_t payload_len = len - 6;

    if (payload_len < 2) {
        ESP_LOGW(TAG, "Special command payload too short: %d", payload_len);
        return;
    }

    uint8_t cmd_id = payload[0];
    uint8_t param_len = payload[1];
    const uint8_t *params = payload + 2;

    if (payload_len < (2 + param_len)) {
        ESP_LOGW(TAG, "Special command parameter length mismatch. Expected %d, got %d", param_len, payload_len - 2);
        return;
    }

    switch (cmd_id) {
        case SPECIAL_CMD_ID_STA_IP:
            if (param_len == 4) {
                ESP_LOGI(TAG, "Received command to set STA IP: %d.%d.%d.%d", params[0], params[1], params[2], params[3]);
            } else {
                ESP_LOGW(TAG, "Invalid param length for STA_IP: %d", param_len);
            }
            break;
        case SPECIAL_CMD_ID_STA_PASSWORD:
            if (param_len > 0 && param_len < 64) {
                char password[65];
                memcpy(password, params, param_len);
                password[param_len] = '\0';
                ESP_LOGI(TAG, "Received command to set STA Password: %s", password);
            } else {
                ESP_LOGW(TAG, "Invalid param length for STA_PASSWORD: %d", param_len);
            }
            break;
        case SPECIAL_CMD_ID_RESTART:
            ESP_LOGI(TAG, "Received command to restart. Restarting...");
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_restart();
            break;
        default:
            ESP_LOGW(TAG, "Unknown special command ID: 0x%02X", cmd_id);
            break;
    }
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