/**
 * @file telemetry.sender.c
 * @brief 处理发送控制帧相关的函数
 * @author TidyCraze
 * @date 2025-08-15
 * @warning 请不要随意调换头文件顺序
 */
#include "telemetry_sender.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lwip/sockets.h"
#include <errno.h>

#include "telemetry_data_converter.h"
#include "telemetry_main.h"
#include "telemetry_protocol.h"
#include <string.h>

static const char* TAG = "telemetry_sender";

// 全局变量
static int g_client_sock = -1;
static bool g_sender_active = false;
static uint32_t g_last_heartbeat = 0;
static uint32_t g_last_data_send = 0;

// 内部函数声明
static int send_frame(const uint8_t* frame, size_t len);

/**
 * @brief 初始化发送器
 *
 * @return 0 成功，-1 失败
 */
int telemetry_sender_init(void) {
    ESP_LOGI(TAG, "Initializing telemetry sender");
    g_client_sock = -1;
    g_sender_active = false;
    g_last_heartbeat = 0;
    g_last_data_send = 0;
    return 0;
}

/**
 * @brief 设置客户端套接字
 *
 * @param client_sock 客户端套接字
 */
void telemetry_sender_set_client_socket(int client_sock) {
    g_client_sock = client_sock;
    g_sender_active = (client_sock >= 0);
    if (g_sender_active) {
        // 连接建立后，立即重置计时器，以尽快发送第一个心跳和数据包
        g_last_heartbeat = xTaskGetTickCount();
        g_last_data_send = xTaskGetTickCount();
        ESP_LOGI(TAG, "Telemetry sender activated with client socket %d", client_sock);
    } else {
        ESP_LOGI(TAG, "Telemetry sender deactivated");
    }
}

/**
 * @brief 检查发送器是否激活
 *
 * @return 是否激活
 */
bool telemetry_sender_is_active(void) { return g_sender_active && g_client_sock >= 0; }

/**
 * @brief 处理发送器
 */
void telemetry_sender_process(void) {
    if (!g_sender_active || g_client_sock < 0) {
        return;
    }

    uint32_t current_time = xTaskGetTickCount();
    uint8_t frame_buffer[128]; // 用于构建帧的缓冲区

    // 每2秒发送心跳包
    if (current_time - g_last_heartbeat > pdMS_TO_TICKS(2000)) {
        uint8_t status = 0;
        if (telemetry_data_converter_get_device_status(&status) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to get device status for heartbeat");
            status = 0x02; // Error status
        }

        size_t frame_len = telemetry_protocol_create_heartbeat_frame(frame_buffer, sizeof(frame_buffer), status);
        if (frame_len > 0) {
            if (send_frame(frame_buffer, frame_len) > 0) {
                ESP_LOGI(TAG, "Sent heartbeat frame");
                g_last_heartbeat = current_time;
            } else {
                ESP_LOGW(TAG, "Failed to send heartbeat, client may be disconnected");
                g_sender_active = false;
                return;
            }
        }
    }

    // 每100毫秒发送控制数据
    if (current_time - g_last_data_send > pdMS_TO_TICKS(100)) {
        uint16_t channels[8];
        uint8_t channel_count = 0;

        if (telemetry_data_converter_get_rc_channels(channels, &channel_count) == ESP_OK) {
            size_t frame_len =
                telemetry_protocol_create_rc_frame(frame_buffer, sizeof(frame_buffer), channel_count, channels);
            if (frame_len > 0) {
                if (send_frame(frame_buffer, frame_len) > 0) {
                    ESP_LOGD(TAG, "Sent RC frame: throttle=%d, direction=%d", channels[0], channels[1]);
                } else {
                    ESP_LOGW(TAG, "Failed to send RC frame");
                    g_sender_active = false;
                    return;
                }
            }
        } else {
            ESP_LOGW(TAG, "Failed to get RC channel data to send");
        }
        g_last_data_send = current_time;
    }
}

void telemetry_sender_deactivate(void) {
    g_sender_active = false;
    g_client_sock = -1;
    ESP_LOGI(TAG, "Telemetry sender manually deactivated");
}

/**
 * @brief 发送帧
 *
 * @param frame 帧数据
 * @param len 帧长度
 * @return 发送的字节数
 */
static int send_frame(const uint8_t* frame, size_t len) {
    if (!g_sender_active || g_client_sock < 0 || frame == NULL || len == 0) {
        return -1;
    }

    int sent = send(g_client_sock, frame, len, 0);
    if (sent < 0) {
        ESP_LOGE(TAG, "Socket send error: %d", errno);
        g_sender_active = false; // 认为连接已断开
        return -1;
    }
    if (sent < len) {
        ESP_LOGW(TAG, "Socket send partial: sent %d of %d bytes", sent, len);
    }
    return sent;
}
