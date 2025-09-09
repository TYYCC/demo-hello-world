#ifndef TELEMETRY_PROTOCOL_H
#define TELEMETRY_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FRAME_HEADER_1 0xAA
#define FRAME_HEADER_2 0x55

// 帧类型
typedef enum {
    FRAME_TYPE_RC = 0x01,
    FRAME_TYPE_TELEMETRY = 0x02,
    FRAME_TYPE_HEARTBEAT = 0x03,
    FRAME_TYPE_EXT_CMD = 0x04,
    FRAME_TYPE_SPECIAL_CMD = 0x05,
    FRAME_TYPE_IMAGE_TRANSFER = 0x06,
    FRAME_TYPE_ACK = 0x07,
} frame_type_t;

// 扩展命令ID
typedef enum {
    EXT_CMD_ID_SET_PWM_FREQ = 0x10,
    EXT_CMD_ID_MODE_SWITCH = 0x11,
    EXT_CMD_ID_CALIBRATE_SENSOR = 0x12,
    EXT_CMD_ID_REQUEST_TELEMETRY = 0x13,
    EXT_CMD_ID_LIGHT_CONTROL = 0x14,
} ext_cmd_id_t;

// 特殊命令ID
typedef enum {
    SPECIAL_CMD_ID_STA_IP = 0x11,
    SPECIAL_CMD_ID_STA_PASSWORD = 0x12,
    SPECIAL_CMD_ID_RESTART = 0x15,
} special_cmd_id_t;

// 图传命令ID
typedef enum {
    IMAGE_CMD_ID_JPEG_QUALITY = 0x20,
    IMAGE_CMD_ID_JPEG_WIDTH = 0x21,
    IMAGE_CMD_ID_JPEG_HEIGHT = 0x22,
    IMAGE_CMD_ID_ENABLE_JPEG = 0x23,
    IMAGE_CMD_ID_USE_UDP = 0x24,
    IMAGE_CMD_ID_UDP_PORT = 0x25,
} image_cmd_id_t;

// ACK命令ID
typedef enum {
    ACK_CMD_ID_IMAGE_SETTING_RESULT = 0x26,
    ACK_CMD_ID_JPEG_ENABLE_RESULT = 0x27,
    ACK_CMD_ID_UDP_USE_RESULT = 0x28,
    ACK_CMD_ID_UDP_PORT_RESULT = 0x2C,
} ack_cmd_id_t;

#pragma pack(push, 1)

// 通用帧头
typedef struct {
    uint8_t header1;
    uint8_t header2;
    uint8_t len;
    uint8_t type;
} telemetry_header_t;

// 遥控命令负载 (地面站 -> ESP32)
typedef struct {
    uint8_t channel_count;
    uint16_t channels[8]; // 最多8通道
} rc_command_payload_t;

// 遥测数据负载 (ESP32 -> 地面站)
typedef struct {
    uint16_t voltage_mv;
    uint16_t current_ma;
    int16_t roll_deg;  // 单位: 0.01°
    int16_t pitch_deg; // 单位: 0.01°
    int16_t yaw_deg;   // 单位: 0.01°
    int32_t altitude_cm;
} telemetry_data_payload_t;

// 心跳包负载 (ESP32 -> 地面站)
typedef struct {
    uint8_t device_status;
} heartbeat_payload_t;

// 扩展命令负载 (地面站 -> ESP32)
typedef struct {
    uint8_t cmd_id;
    uint8_t param_len;
    uint8_t params[];
} ext_command_payload_t;

// 特殊命令负载 (地面站 -> ESP32)
typedef struct {
    uint8_t cmd_id;
    uint8_t param_len;
    uint8_t params[];
} special_command_payload_t;

// 图传命令负载 (地面站 -> ESP32)
typedef struct {
    uint8_t cmd_id;
    uint8_t param_len;
    uint8_t params[];
} image_command_payload_t;

// ACK应答负载 (ESP32 -> 地面站)
typedef struct {
    uint8_t original_frame_type; // 原始帧类型
    uint8_t ack_status;         // ACK状态码
    uint8_t response_len;       // 响应数据长度
    uint8_t response_data[];    // 响应数据
} ack_payload_t;

#pragma pack(pop)

// 结构体用于存放解析后的帧数据
typedef struct {
    telemetry_header_t header;
    const uint8_t* payload;
    size_t payload_len;
    bool crc_ok;
} parsed_frame_t;

/**
 * @brief 编码遥控命令帧
 *
 * @param buffer 用于存储编码后数据的缓冲区
 * @param buffer_size 缓冲区大小
 * @param channel_count 通道数 (1-8)
 * @param channels 通道值数组 (0-1000)
 * @return 编码后的帧长度, 失败返回0
 */
size_t telemetry_protocol_create_rc_frame(uint8_t* buffer, size_t buffer_size,
                                          uint8_t channel_count, const uint16_t* channels);

// 心跳帧创建函数已移除，心跳包由独立的TCP服务器处理

/**
 * @brief 编码扩展命令帧
 *
 * @param buffer 用于存储编码后数据的缓冲区
 * @param buffer_size 缓冲区大小
 * @param cmd_id 扩展命令ID
 * @param params 命令参数
 * @param param_len 参数长度
 * @return 编码后的帧长度, 失败返回0
 */
size_t telemetry_protocol_create_ext_command(uint8_t* buffer, size_t buffer_size, uint8_t cmd_id,
                                             const uint8_t* params, uint8_t param_len);

/**
 * @brief 编码特殊命令帧
 *
 * @param buffer 用于存储编码后数据的缓冲区
 * @param buffer_size 缓冲区大小
 * @param cmd_id 特殊命令ID
 * @param params 命令参数
 * @param param_len 参数长度
 * @return 编码后的帧长度, 失败返回0
 */
size_t telemetry_protocol_create_special_command(uint8_t* buffer, size_t buffer_size, uint8_t cmd_id,
                                                const uint8_t* params, uint8_t param_len);

/**
 * @brief 编码图传命令帧
 *
 * @param buffer 用于存储编码后数据的缓冲区
 * @param buffer_size 缓冲区大小
 * @param cmd_id 图传命令ID
 * @param params 命令参数
 * @param param_len 参数长度
 * @return 编码后的帧长度, 失败返回0
 */
size_t telemetry_protocol_create_image_command(uint8_t* buffer, size_t buffer_size, uint8_t cmd_id,
                                              const uint8_t* params, uint8_t param_len);

/**
 * @brief 解析收到的数据帧
 *
 * @param buffer 接收到的数据流
 * @param len 数据流长度
 * @param frame 解析结果
 * @return 如果成功解析出一个完整的帧，返回该帧的总长度；否则返回0.
 */
size_t telemetry_protocol_parse_frame(const uint8_t* buffer, size_t len, parsed_frame_t* frame);

/**
 * @brief 创建遥测数据帧
 *
 * @param buffer 用于存储编码后数据的缓冲区
 * @param buffer_size 缓冲区大小
 * @param telemetry_data 遥测数据
 * @return 编码后的帧长度, 失败返回0
 */
size_t telemetry_protocol_create_telemetry_frame(uint8_t* buffer, size_t buffer_size,
                                                const telemetry_data_payload_t* telemetry_data);

/**
 * @brief 创建ACK应答帧
 *
 * @param buffer 用于存储编码后数据的缓冲区
 * @param buffer_size 缓冲区大小
 * @param original_frame_type 原始帧类型
 * @param ack_status ACK状态码
 * @param response_data 响应数据
 * @param response_len 响应数据长度
 * @return 编码后的帧长度, 失败返回0
 */
size_t telemetry_protocol_create_ack_frame(uint8_t* buffer, size_t buffer_size,
                                          uint8_t original_frame_type, uint8_t ack_status,
                                          const uint8_t* response_data, size_t response_len);

/**
 * @brief 发送图传配置命令并等待ACK响应
 * @param socket_fd TCP套接字文件描述符
 * @param cmd_id 图传命令ID
 * @param params 命令参数
 * @param param_len 参数长度
 * @param ack_buffer 接收ACK响应的缓冲区
 * @param ack_buffer_size ACK缓冲区大小
 * @param timeout_ms 超时时间(毫秒)
 * @return 接收到的ACK数据长度，0表示失败
 */
size_t telemetry_protocol_send_image_config_and_wait_ack(int socket_fd, uint8_t cmd_id,
                                                        const uint8_t* params, uint8_t param_len,
                                                        uint8_t* ack_buffer, size_t ack_buffer_size,
                                                        uint32_t timeout_ms);

/**
 * @brief 解析ACK响应帧
 * @param buffer ACK帧数据
 * @param buffer_size 数据长度
 * @param original_frame_type 输出：原始帧类型
 * @param ack_status 输出：ACK状态码
 * @param response_data 输出：响应数据指针
 * @param response_len 输出：响应数据长度
 * @return true解析成功，false解析失败
 */
bool telemetry_protocol_parse_ack_frame(const uint8_t* buffer, size_t buffer_size,
                                       uint8_t* original_frame_type, uint8_t* ack_status,
                                       const uint8_t** response_data, size_t* response_len);

/**
 * @brief 计算CRC16校验值
 * @param data 数据指针
 * @param length 数据长度
 * @return CRC16校验值
 */
uint16_t crc16_modbus_table(const uint8_t* data, uint16_t length);

#endif // TELEMETRY_PROTOCOL_H
