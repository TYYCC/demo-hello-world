#ifndef TELEMETRY_PROTOCOL_H
#define TELEMETRY_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * ============================================================================
 * ELRS协议兼容遥测实现
 * ============================================================================
 * 本模块实现基于ExpressLRS (ELRS) OTA协议的遥测数据包格式
 * 支持链路统计、遥控通道数据和扩展遥测信息的传输
 */

// ============================================================================
// ELRS OTA LinkStats 结构体 (下行遥测)
// ============================================================================

#pragma pack(push, 1)

/**
 * @brief ELRS 链路统计数据结构
 * 对应OTA_LinkStats_s，包含无线链路质量信息
 */
typedef struct {
    uint8_t uplink_rssi_1;        // 天线1 RSSI (dBm + 120偏移, 0-255)
    uint8_t uplink_rssi_2;        // 天线2 RSSI (dBm + 120偏移, 0-255)
    uint8_t link_quality;         // 链路质量LQ (0-100%)，bit7=diversity可用标志
    int8_t snr;                   // 信噪比 (dB)
} elrs_link_stats_t;

/**
 * @brief ELRS OTA 4字节数据包 (简化模式，4通道10bit + 7位开关)
 */
typedef struct {
    uint8_t type_crc_high;        // bit[1:0]=包类型, bit[7:2]=CRC高6位
    uint8_t ch0_ch1_low;          // CH0和CH1的10bit数据 (前10bit)
    uint8_t ch1_ch2_low;          // CH1和CH2的10bit数据
    uint8_t ch2_ch3_low;          // CH2和CH3的10bit数据
    uint8_t ch3_switches;         // CH3和开关数据
    union {
        struct {
            uint8_t switches;     // 开关数据 (bit[6:0]), 遥测ACK (bit7)
            uint8_t is_armed;     // 解锁状态
        } rc;
        struct {
            elrs_link_stats_t stats;  // 链路统计
            uint8_t payload[1];   // 额外遥测数据
        } link_stats;
    };
    uint8_t crc_low;              // CRC低8位
} elrs_ota_packet4_t;

/**
 * @brief ELRS OTA 8字节数据包 (完整模式，16通道)
 */
typedef struct {
    uint8_t type_flags;           // bit[1:0]=包类型, bit[7:2]=各种标志
    uint8_t ch0_ch1_low[2];       // CH0-CH1原始10bit数据
    uint8_t ch2_ch3_low[2];       // CH2-CH3原始10bit数据
    uint8_t ch4_ch7_high[2];      // CH4-CH7原始10bit数据
    union {
        elrs_link_stats_t stats;  // 链路统计
        uint8_t payload[4];       // 其他数据
    };
    uint16_t crc;                 // CRC16
} elrs_ota_packet8_t;

/**
 * @brief 遥测扩展数据负载 (用于传输IMU、电池等信息)
 * 通过ELRS数据下行通道传输
 */
typedef struct {
    float voltage;                // 电压 (V)
    float current;                // 电流 (A)
    float roll;                   // 横滚角 (度)
    float pitch;                  // 俯仰角 (度)
    float yaw;                    // 偏航角 (度)
    float altitude;               // 高度 (米)
    uint32_t timestamp_ms;        // 时间戳
} elrs_extended_telemetry_t;

#pragma pack(pop)

// ============================================================================
// 通道值定义 (CRSF标准)
// ============================================================================

#define CRSF_CHANNEL_VALUE_MIN      172   // 987us (标准最小)
#define CRSF_CHANNEL_VALUE_1000     191   // 1000us
#define CRSF_CHANNEL_VALUE_MID      992   // 1500us (中间值)
#define CRSF_CHANNEL_VALUE_2000     1792  // 2000us
#define CRSF_CHANNEL_VALUE_MAX      1811  // 2012us (标准最大)

// ============================================================================
// ELRS 协议相关定义
// ============================================================================

#define ELRS_OTA4_PACKET_SIZE       8     // 4字节包长度
#define ELRS_OTA8_PACKET_SIZE       13    // 8字节包长度
#define ELRS_MAX_CHANNELS           16    // 最大通道数
#define ELRS_NUM_ANTENNAS           2     // 天线数量

// OTA数据包类型
typedef enum {
    ELRS_PACKET_TYPE_RCDATA = 0b00,      // 遥控数据
    ELRS_PACKET_TYPE_DATA = 0b01,        // 通用数据
    ELRS_PACKET_TYPE_SYNC = 0b10,        // 同步包
    ELRS_PACKET_TYPE_LINKSTATS = 0b00    // 链路统计 (下行)
} elrs_packet_type_e;

// ============================================================================
// 函数声明
// ============================================================================

/**
 * @brief 将通道值 (0-2047 CRSF格式) 转换为PWM微秒
 * @param crsf_value CRSF通道值
 * @return PWM微秒值 (880-2120us)
 */
uint16_t telemetry_crsf_to_pwm_us(uint16_t crsf_value);

/**
 * @brief 将PWM微秒转换为CRSF通道值
 * @param pwm_us PWM微秒值 (880-2120us)
 * @return CRSF通道值 (0-2047)
 */
uint16_t telemetry_pwm_us_to_crsf(uint16_t pwm_us);

/**
 * @brief 将旧格式遥测数据转换为新的ELRS格式
 * @param old_payload 旧协议数据
 * @param new_data 输出的ELRS遥测数据
 * @return 成功返回0，失败返回-1
 */
int telemetry_protocol_convert_old_format(const void* old_payload, elrs_link_stats_t* new_data);

/**
 * @brief 编码ELRS链路统计数据包
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @param rssi_1 天线1 RSSI (dBm)
 * @param rssi_2 天线2 RSSI (dBm)
 * @param link_quality 链路质量 (0-100)
 * @param snr 信噪比 (dB)
 * @return 编码后的数据长度
 */
size_t telemetry_protocol_create_link_stats(uint8_t* buffer, size_t buffer_size,
                                            int16_t rssi_1, int16_t rssi_2,
                                            uint8_t link_quality, int8_t snr);

#endif // TELEMETRY_PROTOCOL_H
