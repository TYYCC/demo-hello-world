#include "telemetry_protocol.h"
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

/**
 * @brief CRC16 查表
 */
static const uint16_t crc16_table[256] = {
    0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241, 0xC601, 0x06C0, 0x0780, 0xC741,
    0x0500, 0xC5C1, 0xC481, 0x0440, 0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
    0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841, 0xD801, 0x18C0, 0x1980, 0xD941,
    0x1B00, 0xDBC1, 0xDA81, 0x1A40, 0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
    0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641, 0xD201, 0x12C0, 0x1380, 0xD341,
    0x1100, 0xD1C1, 0xD081, 0x1040, 0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
    0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441, 0x3C00, 0xFCC1, 0xFD81, 0x3D40,
    0xFF01, 0x3FC0, 0x3E80, 0xFE41, 0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
    0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41, 0xEE01, 0x2EC0, 0x2F80, 0xEF41,
    0x2D00, 0xEDC1, 0xEC81, 0x2C40, 0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
    0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041, 0xA001, 0x60C0, 0x6180, 0xA141,
    0x6300, 0xA3C1, 0xA281, 0x6240, 0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
    0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41, 0xAA01, 0x6AC0, 0x6B80, 0xAB41,
    0x6900, 0xA9C1, 0xA881, 0x6840, 0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
    0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40, 0xB401, 0x74C0, 0x7580, 0xB541,
    0x7700, 0xB7C1, 0xB681, 0x7640, 0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
    0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241, 0x9601, 0x56C0, 0x5780, 0x9741,
    0x5500, 0x95C1, 0x9481, 0x5440, 0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
    0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841, 0x8801, 0x48C0, 0x4980, 0x8941,
    0x4B00, 0x8BC1, 0x8A81, 0x4A40, 0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
    0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641, 0x8201, 0x42C0, 0x4380, 0x8341,
    0x4100, 0x81C1, 0x8081, 0x4040};

/**
 * @brief 查表计算 CRC16
 *
 * @param data 待计算数据指针
 * @param length 数据长度
 * @return uint16_t CRC16 校验码
 */
uint16_t crc16_modbus_table(const uint8_t* data, uint16_t length) {
    uint8_t index;
    uint16_t crc = 0xFFFF;

    while (length--) {
        index = (uint8_t)(crc ^ *data++);
        crc = (crc >> 8) ^ crc16_table[index];
    }
    return crc;
}

/**
 * @brief 填充帧头、长度、类型，计算并填充CRC
 *
 * @param buffer 帧缓冲区 (至少需要4字节空间给header)
 * @param type 帧类型
 * @param payload 负载数据指针
 * @param payload_len 负载数据长度
 * @return 整个帧的总长度
 */
static size_t finalize_frame(uint8_t* buffer, frame_type_t type, const uint8_t* payload,
                             size_t payload_len) {
    // 1. 填充帧头和类型
    buffer[0] = FRAME_HEADER_1;
    buffer[1] = FRAME_HEADER_2;
    buffer[3] = (uint8_t)type;

    // 2. 填充长度字段 (Length = 1 byte for Type + Payload Length)
    uint8_t length_field = 1 + payload_len;
    buffer[2] = length_field;

    // 3. 复制负载
    if (payload && payload_len > 0) {
        memcpy(&buffer[4], payload, payload_len);
    }

    // 4. 计算并填充 CRC
    // CRC is calculated over: Length_Field (1B) + Type_Field (1B) + Payload (N B)
    uint16_t crc = crc16_modbus_table(&buffer[2], 1 + 1 + payload_len);
    size_t crc_offset = 4 + payload_len;
    buffer[crc_offset] = crc & 0xFF;
    buffer[crc_offset + 1] = (crc >> 8) & 0xFF;

    // 整个帧的长度 = Header(2) + Length(1) + Type(1) + Payload(N) + CRC(2)
    return 2 + 1 + 1 + payload_len + 2;
}

/**
 * @brief 创建遥控数据帧
 *
 * @param buffer 帧缓冲区
 * @param buffer_size 帧缓冲区大小
 * @param channel_count 通道数量
 * @param channels 通道数据数组
 * @return 整个帧的总长度
 */
size_t telemetry_protocol_create_rc_frame(uint8_t* buffer, size_t buffer_size,
                                          uint8_t channel_count, const uint16_t* channels) {
    if (channel_count == 0 || channel_count > 8) {
        return 0;
    }

    uint8_t payload[1 + 8 * sizeof(uint16_t)]; // Max payload size
    size_t payload_len = 1 + channel_count * sizeof(uint16_t);

    payload[0] = channel_count;
    memcpy(&payload[1], channels, channel_count * sizeof(uint16_t));

    size_t frame_len = 2 + 1 + 1 + payload_len + 2; // Header + Len + Type + Payload + CRC
    if (buffer_size < frame_len) {
        return 0;
    }

    return finalize_frame(buffer, FRAME_TYPE_RC, payload, payload_len);
}

// 心跳帧创建函数已移除，心跳包由独立的TCP服务器处理

/**
 * @brief 创建扩展命令帧
 *
 * @param buffer 帧缓冲区
 * @param buffer_size 帧缓冲区大小
 * @param cmd_id 命令ID
 * @param params 参数数组
 * @param param_len 参数长度
 * @return 整个帧的总长度
 */
size_t telemetry_protocol_create_ext_command(uint8_t* buffer, size_t buffer_size, uint8_t cmd_id,
                                             const uint8_t* params, uint8_t param_len) {
    size_t payload_len = 1 + 1 + param_len; // cmd_id + param_len + params
    uint8_t payload[payload_len];

    payload[0] = cmd_id;
    payload[1] = param_len;
    if (param_len > 0 && params != NULL) {
        memcpy(&payload[2], params, param_len);
    }

    size_t frame_len = 2 + 1 + 1 + payload_len + 2; // Header + Len + Type + Payload + CRC
    if (buffer_size < frame_len) {
        return 0;
    }

    return finalize_frame(buffer, FRAME_TYPE_EXT_CMD, payload, payload_len);
}

/**
 * @brief 创建特殊命令帧
 *
 * @param buffer 帧缓冲区
 * @param buffer_size 帧缓冲区大小
 * @param cmd_id 特殊命令ID
 * @param params 参数数组
 * @param param_len 参数长度
 * @return 整个帧的总长度
 */
size_t telemetry_protocol_create_special_command(uint8_t* buffer, size_t buffer_size, uint8_t cmd_id,
                                                const uint8_t* params, uint8_t param_len) {
    size_t payload_len = 1 + 1 + param_len; // cmd_id + param_len + params
    uint8_t payload[payload_len];

    payload[0] = cmd_id;
    payload[1] = param_len;
    if (param_len > 0 && params != NULL) {
        memcpy(&payload[2], params, param_len);
    }

    size_t frame_len = 2 + 1 + 1 + payload_len + 2; // Header + Len + Type + Payload + CRC
    if (buffer_size < frame_len) {
        return 0;
    }

    return finalize_frame(buffer, FRAME_TYPE_SPECIAL_CMD, payload, payload_len);
}

/**
 * @brief 创建图传命令帧
 *
 * @param buffer 帧缓冲区
 * @param buffer_size 帧缓冲区大小
 * @param cmd_id 图传命令ID
 * @param params 参数数组
 * @param param_len 参数长度
 * @return 整个帧的总长度
 */
size_t telemetry_protocol_create_image_command(uint8_t* buffer, size_t buffer_size, uint8_t cmd_id,
                                              const uint8_t* params, uint8_t param_len) {
    size_t payload_len = 1 + 1 + param_len; // cmd_id + param_len + params
    uint8_t payload[payload_len];

    payload[0] = cmd_id;
    payload[1] = param_len;
    if (param_len > 0 && params != NULL) {
        memcpy(&payload[2], params, param_len);
    }

    size_t frame_len = 2 + 1 + 1 + payload_len + 2; // Header + Len + Type + Payload + CRC
    if (buffer_size < frame_len) {
        return 0;
    }

    return finalize_frame(buffer, FRAME_TYPE_IMAGE_TRANSFER, payload, payload_len);
}

/**
 * @brief 创建遥测数据帧
 *
 * @param buffer 帧缓冲区
 * @param buffer_size 帧缓冲区大小
 * @param telemetry_data 遥测数据
 * @return 整个帧的总长度
 */
size_t telemetry_protocol_create_telemetry_frame(uint8_t* buffer, size_t buffer_size,
                                                const telemetry_data_payload_t* telemetry_data) {
    if (!buffer || !telemetry_data) {
        return 0;
    }

    size_t payload_len = sizeof(telemetry_data_payload_t);
    size_t frame_len = 2 + 1 + 1 + payload_len + 2; // Header + Len + Type + Payload + CRC
    
    if (buffer_size < frame_len) {
        return 0;
    }

    return finalize_frame(buffer, FRAME_TYPE_TELEMETRY, (const uint8_t*)telemetry_data, payload_len);
}

/**
 * @brief 创建ACK应答帧
 *
 * @param buffer 帧缓冲区
 * @param buffer_size 帧缓冲区大小
 * @param original_frame_type 原始帧类型
 * @param ack_status ACK状态码
 * @param response_data 响应数据
 * @param response_len 响应数据长度
 * @return 整个帧的总长度
 */
size_t telemetry_protocol_create_ack_frame(uint8_t* buffer, size_t buffer_size,
                                          uint8_t original_frame_type, uint8_t ack_status,
                                          const uint8_t* response_data, size_t response_len) {
    if (!buffer) {
        return 0;
    }

    // 限制响应数据长度
    if (response_len > 64) {
        response_len = 64;
    }

    size_t payload_len = 3 + response_len; // original_frame_type + ack_status + response_len + response_data
    size_t frame_len = 2 + 1 + 1 + payload_len + 2; // Header + Len + Type + Payload + CRC
    
    if (buffer_size < frame_len) {
        return 0;
    }

    // 构建负载
    uint8_t payload[67]; // 3 + 64 max
    payload[0] = original_frame_type;
    payload[1] = ack_status;
    payload[2] = (uint8_t)response_len;
    
    if (response_data && response_len > 0) {
        memcpy(&payload[3], response_data, response_len);
    }

    return finalize_frame(buffer, FRAME_TYPE_ACK, payload, payload_len);
}

/**
 * @brief 解析帧
 *
 * @param buffer 帧缓冲区
 * @param len 帧长度
 * @param frame 解析结果结构体
 * @return 整个帧的总长度
 */
size_t telemetry_protocol_parse_frame(const uint8_t* buffer, size_t len, parsed_frame_t* frame) {
    if (buffer == NULL || len < 7 ||
        frame == NULL) { // 最小帧长度: 头(2)+长(1)+类型(1)+CRC(2) = 6. 心跳包是7
        return 0;
    }

    // 1. 查找帧头
    if (buffer[0] != FRAME_HEADER_1 || buffer[1] != FRAME_HEADER_2) {
        return 0;
    }

    // 2. 获取长度字段并计算完整帧长
    uint8_t length_field = buffer[2];
    // 协议定义 length_field = 1(Type) + N(Payload)
    // 完整帧长 = 2(Header) + 1(Length) + length_field + 2(CRC)
    size_t total_frame_len = 2 + 1 + length_field + 2;

    if (len < total_frame_len) {
        // 数据包不完整
        return 0;
    }

    // 3. 校验CRC
    uint16_t received_crc =
        (uint16_t)(buffer[total_frame_len - 1] << 8) | buffer[total_frame_len - 2];
    // CRC 计算范围: Length_Field (1B) + Type_Field (1B) + Payload (N B)
    // 总长度为 length_field + 1
    uint16_t calculated_crc = crc16_modbus_table(&buffer[2], length_field + 1);

    // 4. 填充解析结果结构体
    frame->header.header1 = buffer[0];
    frame->header.header2 = buffer[1];
    frame->header.len = length_field;
    frame->header.type = buffer[3];
    frame->payload_len = length_field - 1;
    frame->payload = &buffer[4];
    frame->crc_ok = (received_crc == calculated_crc);

    return total_frame_len;
}

/**
 * @brief 发送图传配置命令并等待ACK响应
 */
size_t telemetry_protocol_send_image_config_and_wait_ack(int socket_fd, uint8_t cmd_id,
                                                        const uint8_t* params, uint8_t param_len,
                                                        uint8_t* ack_buffer, size_t ack_buffer_size,
                                                        uint32_t timeout_ms) {
    if (socket_fd < 0 || !ack_buffer || ack_buffer_size == 0) {
        return 0;
    }

    // 创建图传命令帧
    uint8_t cmd_buffer[256];
    size_t cmd_len = telemetry_protocol_create_image_command(cmd_buffer, sizeof(cmd_buffer),
                                                           cmd_id, params, param_len);
    if (cmd_len == 0) {
        return 0;
    }

    // 发送命令
    ssize_t sent = send(socket_fd, cmd_buffer, cmd_len, 0);
    if (sent != (ssize_t)cmd_len) {
        return 0;
    }

    // 设置接收超时
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        return 0;
    }

    // 接收ACK响应
    ssize_t received = recv(socket_fd, ack_buffer, ack_buffer_size, 0);
    if (received <= 0) {
        return 0;
    }

    return (size_t)received;
}

/**
 * @brief 解析ACK响应帧
 */
bool telemetry_protocol_parse_ack_frame(const uint8_t* buffer, size_t buffer_size,
                                       uint8_t* original_frame_type, uint8_t* ack_status,
                                       const uint8_t** response_data, size_t* response_len) {
    if (!buffer || !original_frame_type || !ack_status || !response_data || !response_len) {
        return false;
    }

    // 解析帧
    parsed_frame_t parsed_frame;
    size_t frame_len = telemetry_protocol_parse_frame(buffer, buffer_size, &parsed_frame);
    if (frame_len == 0 || !parsed_frame.crc_ok) {
        return false;
    }

    // 检查是否为ACK帧
    if (parsed_frame.header.type != FRAME_TYPE_ACK) {
        return false;
    }

    // 检查负载长度
    if (parsed_frame.payload_len < 3) { // 至少包含original_frame_type, ack_status, response_len
        return false;
    }

    // 解析ACK负载
    const uint8_t* payload = parsed_frame.payload;
    *original_frame_type = payload[0];
    *ack_status = payload[1];
    *response_len = payload[2];

    // 检查响应数据长度是否合理
    if (*response_len > parsed_frame.payload_len - 3) {
        return false;
    }

    // 设置响应数据指针
    if (*response_len > 0) {
        *response_data = &payload[3];
    } else {
        *response_data = NULL;
    }

    return true;
}
