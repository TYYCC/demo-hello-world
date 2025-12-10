#include "telemetry_protocol.h"
#include <string.h>
#include <stdint.h>

/**
 * @brief 将CRSF通道值转换为PWM微秒
 * CRSF范围: 0-2047 (11-bit)
 * PWM范围: 880-2120 微秒
 */
uint16_t telemetry_crsf_to_pwm_us(uint16_t crsf_value) {
    // 限制值在有效范围内
    if (crsf_value > 2047) {
        crsf_value = 2047;
    }
    
    // CRSF到PWM转换公式:
    // PWM = (CRSF_VALUE - 0) * (2120 - 880) / 2047 + 880
    // PWM = CRSF_VALUE * 1240 / 2047 + 880
    uint32_t pwm = ((uint32_t)crsf_value * 1240 / 2047) + 880;
    
    return (uint16_t)pwm;
}

/**
 * @brief 将PWM微秒转换为CRSF通道值
 * PWM范围: 880-2120 微秒
 * CRSF范围: 0-2047 (11-bit)
 */
uint16_t telemetry_pwm_us_to_crsf(uint16_t pwm_us) {
    // 限制值在有效范围内
    if (pwm_us < 880) {
        pwm_us = 880;
    }
    if (pwm_us > 2120) {
        pwm_us = 2120;
    }
    
    // PWM到CRSF转换公式:
    // CRSF = (PWM - 880) * 2047 / (2120 - 880)
    // CRSF = (PWM - 880) * 2047 / 1240
    uint32_t crsf = ((uint32_t)(pwm_us - 880) * 2047) / 1240;
    
    return (uint16_t)crsf;
}

/**
 * @brief 编码ELRS链路统计数据
 * 格式遵循ELRS OTA_LinkStats_s结构
 * 
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小 (需要至少4字节)
 * @param rssi_1 天线1 RSSI (dBm)
 * @param rssi_2 天线2 RSSI (dBm)
 * @param link_quality 链路质量 (0-100)
 * @param snr 信噪比 (dB)
 * @return 编码后的数据长度 (4字节)
 */
size_t telemetry_protocol_create_link_stats(uint8_t* buffer, size_t buffer_size,
                                            int16_t rssi_1, int16_t rssi_2,
                                            uint8_t link_quality, int8_t snr) {
    if (!buffer || buffer_size < sizeof(elrs_link_stats_t)) {
        return 0;
    }
    
    elrs_link_stats_t* stats = (elrs_link_stats_t*)buffer;
    
    // RSSI: 存储格式为 dBm + 120 (使值为正)
    // 有效范围: -120 dBm (0) 到 135 dBm (255)
    stats->uplink_rssi_1 = (uint8_t)(rssi_1 + 120);
    stats->uplink_rssi_2 = (uint8_t)(rssi_2 + 120);
    
    // 链路质量: 0-100%
    if (link_quality > 100) {
        link_quality = 100;
    }
    stats->link_quality = link_quality;
    
    // SNR: 原始值存储
    stats->snr = snr;
    
    return sizeof(elrs_link_stats_t);
}

