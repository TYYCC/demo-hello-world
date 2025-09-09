/**
 * @file tcp_command_server.c
 * @brief TCP命令客户端实现 - 支持发送命令和ACK帧创建
 * @author TidyCraze
 * @date 2025-01-27
 */

#include "tcp_command_server.h"
#include "telemetry_protocol.h"
#include "esp_log.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <lwip/errno.h>

static const char* TAG = "TCP_CMD_CLIENT";

// 客户端发送图传配置命令并等待ACK响应
ack_status_t tcp_command_server_send_image_config(const char* server_ip, uint16_t server_port,
                                                  const uint8_t* config_data, size_t config_len,
                                                  uint32_t timeout_ms) {
    if (!server_ip || !config_data || config_len == 0) {
        return ACK_STATUS_PARAMETER_ERROR;
    }

    int client_socket = -1;
    ack_status_t result = ACK_STATUS_ERROR;
    
    // 创建客户端套接字
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        return ACK_STATUS_ERROR;
    }

    // 设置连接超时
    struct timeval connect_timeout;
    connect_timeout.tv_sec = timeout_ms / 1000;
    connect_timeout.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &connect_timeout, sizeof(connect_timeout));
    setsockopt(client_socket, SOL_SOCKET, SO_SNDTIMEO, &connect_timeout, sizeof(connect_timeout));

    // 连接到服务器
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        close(client_socket);
        return ACK_STATUS_PARAMETER_ERROR;
    }

    if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(client_socket);
        return ACK_STATUS_ERROR;
    }

    // 创建图传配置命令帧
    uint8_t command_buffer[256];
    size_t command_len = telemetry_protocol_create_image_command(command_buffer, sizeof(command_buffer),
                                                               IMAGE_CMD_ID_JPEG_QUALITY, config_data, config_len);
    
    if (command_len == 0) {
        close(client_socket);
        return ACK_STATUS_ERROR;
    }

    // 发送命令
    int sent = send(client_socket, command_buffer, command_len, 0);
    if (sent != command_len) {
        close(client_socket);
        return ACK_STATUS_ERROR;
    }

    // 等待ACK响应
    uint8_t recv_buffer[128];
    int recv_len = recv(client_socket, recv_buffer, sizeof(recv_buffer), 0);
    
    if (recv_len <= 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            result = ACK_STATUS_TIMEOUT;
        } else {
            result = ACK_STATUS_ERROR;
        }
    } else {
        // 解析ACK响应
        parsed_frame_t parsed_frame;
        size_t frame_len = telemetry_protocol_parse_frame(recv_buffer, recv_len, &parsed_frame);
        
        if (frame_len > 0 && parsed_frame.crc_ok && parsed_frame.header.type == 0x07) { // ACK帧类型
            if (parsed_frame.payload_len >= 2) { // 至少需要：命令ID(1B) + 参数(1B)
                uint8_t command_id = parsed_frame.payload[0];
                uint8_t ack_result = parsed_frame.payload[1];
                
                // 检查是否为图传相关的ACK命令ID
                if ((command_id >= 0x26 && command_id <= 0x28) || command_id == 0x2C) {
                    result = (ack_status_t)ack_result;
                } else {
                    result = ACK_STATUS_ERROR;
                }
            } else {
                result = ACK_STATUS_ERROR;
            }
        } else {
            result = ACK_STATUS_ERROR;
        }
    }

    // 关闭连接
    close(client_socket);
    
    return result;
}