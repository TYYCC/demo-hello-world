#ifndef TELEMETRY_PROTOCOL_H
#define TELEMETRY_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#define FRAME_HEADER_1 0xAA
#define FRAME_HEADER_2 0x55

// 帧类型
typedef enum {
    FRAME_TYPE_RC = 0x01,
    FRAME_TYPE_TELEMETRY = 0x02,
    FRAME_TYPE_HEARTBEAT = 0x03,
    FRAME_TYPE_EXT_CMD = 0x04,
} frame_type_t;

// 扩展命令ID
typedef enum {
    EXT_CMD_ID_SET_PWM_FREQ = 0x10,
    EXT_CMD_ID_MODE_SWITCH = 0x11,
    EXT_CMD_ID_CALIBRATE_SENSOR = 0x12,
    EXT_CMD_ID_REQUEST_TELEMETRY = 0x13,
    EXT_CMD_ID_LIGHT_CONTROL = 0x14,
} ext_cmd_id_t;

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


#pragma pack(pop)

/**
 * @brief 编码遥控命令帧
 *
 * @param buffer 用于存储编码后数据的缓冲区
 * @param buffer_size 缓冲区大小
 * @param channel_count 通道数 (1-8)
 * @param channels 通道值数组 (0-1000)
 * @return 编码后的帧长度, 失败返回0
 */
size_t telemetry_encode_rc_command(uint8_t *buffer, size_t buffer_size, uint8_t channel_count, const uint16_t *channels);

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
size_t telemetry_encode_ext_command(uint8_t *buffer, size_t buffer_size, uint8_t cmd_id, const uint8_t *params, uint8_t param_len);

#endif // TELEMETRY_PROTOCOL_H
