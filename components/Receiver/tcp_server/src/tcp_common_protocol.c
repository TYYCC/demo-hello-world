/**
 * @file tcp_common_protocol.c
 * @brief TCP通信协议公共实现
 * @author TidyCraze
 * @date 2025-09-05
 */

#include "../inc/tcp_common_protocol.h"
#include "esp_log.h"
#include <string.h>

// CRC16 Modbus查找表
static const uint16_t crc16_table[256] = {
    0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
    0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
    0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
    0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
    0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
    0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
    0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
    0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
    0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
    0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
    0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
    0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
    0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
    0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
    0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
    0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
    0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
    0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
    0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
    0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
    0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
    0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
    0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
    0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
    0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
    0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
    0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
    0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
    0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
    0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
    0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
    0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040
};

uint16_t calculate_crc16_modbus(const uint8_t *data, uint16_t length) {
    uint16_t crc = 0xFFFF;
    
    for (uint16_t i = 0; i < length; i++) {
        uint8_t tbl_idx = ((crc ^ data[i]) & 0xFF);
        crc = ((crc >> 8) ^ crc16_table[tbl_idx]);
    }
    
    return crc;
}

uint16_t create_heartbeat_frame(uint8_t *buffer, uint16_t buffer_size, 
                               uint8_t device_status, uint32_t timestamp) {
    if (!buffer || buffer_size < sizeof(protocol_header_t) + sizeof(heartbeat_payload_t) + sizeof(uint16_t)) {
        return 0;
    }
    
    // 直接操作缓冲区，使用Telemetry协议格式
    uint8_t *ptr = buffer;
    
    // 填充协议头 (兼容Telemetry协议)
    protocol_header_t *header = (protocol_header_t *)ptr;
    header->header1 = FRAME_HEADER_1;                    // 0xAA
    header->header2 = FRAME_HEADER_2;                    // 0x55
    header->length = 1 + sizeof(heartbeat_payload_t);    // 长度 = 1(类型) + N(负载)
    header->frame_type = FRAME_TYPE_HEARTBEAT;           // 0x03
    ptr += sizeof(protocol_header_t);
    
    // 填充心跳负载
    heartbeat_payload_t *payload = (heartbeat_payload_t *)ptr;
    payload->device_status = device_status;
    payload->timestamp = timestamp;
    ptr += sizeof(heartbeat_payload_t);
    
    // 计算CRC（Telemetry协议方式：长度字段 + 类型字段 + 负载数据）
    uint16_t crc_length = 1 + 1 + sizeof(heartbeat_payload_t); // length + type + payload
    uint16_t calculated_crc = calculate_crc16_modbus(&buffer[2], crc_length); // 从长度字段开始
    
    // 直接将CRC写入到正确位置（小端字节序）
    *ptr++ = calculated_crc & 0xFF;        // CRC低字节
    *ptr++ = (calculated_crc >> 8) & 0xFF; // CRC高字节
    
    return sizeof(protocol_header_t) + sizeof(heartbeat_payload_t) + sizeof(uint16_t);
}

uint16_t create_telemetry_frame_common(uint8_t *buffer, uint16_t buffer_size, 
                                      const telemetry_data_payload_t *telemetry_data) {
    if (!buffer || !telemetry_data || buffer_size < sizeof(protocol_header_t) + sizeof(telemetry_data_payload_t) + sizeof(uint16_t)) {
        return 0;
    }
    
    // 直接操作缓冲区，使用Telemetry协议格式
    uint8_t *ptr = buffer;
    
    // 填充协议头 (兼容Telemetry协议)
    protocol_header_t *header = (protocol_header_t *)ptr;
    header->header1 = FRAME_HEADER_1;                    // 0xAA
    header->header2 = FRAME_HEADER_2;                    // 0x55
    header->length = 1 + sizeof(telemetry_data_payload_t); // 长度 = 1(类型) + N(负载)
    header->frame_type = FRAME_TYPE_TELEMETRY;           // 0x02
    ptr += sizeof(protocol_header_t);
    
    // 填充遥测负载
    memcpy(ptr, telemetry_data, sizeof(telemetry_data_payload_t));
    ptr += sizeof(telemetry_data_payload_t);
    
    // 计算CRC（Telemetry协议方式：长度字段 + 类型字段 + 负载数据）
    uint16_t crc_length = 1 + 1 + sizeof(telemetry_data_payload_t); // length + type + payload
    uint16_t calculated_crc = calculate_crc16_modbus(&buffer[2], crc_length); // 从长度字段开始
    
    // 直接将CRC写入到正确位置（小端字节序）
    *ptr++ = calculated_crc & 0xFF;        // CRC低字节
    *ptr++ = (calculated_crc >> 8) & 0xFF; // CRC高字节
    
    uint16_t total_length = sizeof(protocol_header_t) + sizeof(telemetry_data_payload_t) + sizeof(uint16_t);
    return total_length;
}

bool validate_frame(const uint8_t *buffer, uint16_t buffer_size) {
    if (!buffer || buffer_size < MIN_FRAME_SIZE) {
        return false;
    }
    
    // 验证帧头
    if (buffer[0] != FRAME_HEADER_1 || buffer[1] != FRAME_HEADER_2) {
        return false;
    }
    
    // 获取长度字段和帧类型
    uint8_t length_field = buffer[2];
    uint8_t frame_type = buffer[3];
    
    // 验证帧类型
    if (frame_type != FRAME_TYPE_HEARTBEAT && 
        frame_type != FRAME_TYPE_TELEMETRY &&
        frame_type != FRAME_TYPE_COMMAND &&
        frame_type != FRAME_TYPE_EXTENDED) {
        return false;
    }
    
    // 验证帧长度
    // 完整帧长 = 2(帧头) + 1(长度) + length_field + 2(CRC)
    uint16_t expected_size = 2 + 1 + length_field + 2;
    if (buffer_size != expected_size) {
        return false;
    }
    
    // 验证CRC (计算范围：长度字段 + 类型字段 + 负载数据)
    uint16_t crc_length = 1 + length_field; // length + type + payload
    uint16_t calculated_crc = calculate_crc16_modbus(&buffer[2], crc_length);
    
    // 获取接收到的CRC (小端字节序)
    uint16_t received_crc = (uint16_t)(buffer[buffer_size - 1] << 8) | buffer[buffer_size - 2];
    
    if (received_crc != calculated_crc) {
        return false;
    }
    
    return true;
}