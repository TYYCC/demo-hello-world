/**
 * @file telemetry_data_converter.h
 * @brief 本地传感器数据获取和转换模块头文件
 * @author Your Name
 * @date 2024
 */

#ifndef TELEMETRY_DATA_CONVERTER_H
#define TELEMETRY_DATA_CONVERTER_H

#include "esp_err.h"
#include "telemetry_protocol.h"
#include "telemetry_main.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ==================== 数据结构定义 ====================

/**
 * @brief 摇杆数据结构
 */
typedef struct {
    int16_t joy_x;      // X轴数据 (-100~100)
    int16_t joy_y;      // Y轴数据 (-100~100)
    bool valid;         // 数据有效性
} joystick_sensor_data_t;

/**
 * @brief IMU数据结构
 */
typedef struct {
    float roll;         // 横滚角 (度)
    float pitch;        // 俯仰角 (度)
    float yaw;          // 偏航角 (度)
    bool valid;         // 数据有效性
} imu_sensor_data_t;

/**
 * @brief 电池数据结构
 */
typedef struct {
    uint16_t voltage_mv;    // 电压 (mV)
    uint16_t current_ma;    // 电流 (mA)
    bool valid;             // 数据有效性
} battery_sensor_data_t;

/**
 * @brief GPS数据结构 (预留)
 */
typedef struct {
    double latitude;        // 纬度
    double longitude;       // 经度
    float altitude_m;       // 海拔高度 (米)
    bool valid;             // 数据有效性
} gps_sensor_data_t;

/**
 * @brief 本地传感器数据汇总
 */
typedef struct {
    joystick_sensor_data_t joystick;    // 摇杆数据
    imu_sensor_data_t imu;              // IMU数据
    battery_sensor_data_t battery;      // 电池数据
    gps_sensor_data_t gps;              // GPS数据 (预留)
    uint64_t timestamp_ms;              // 时间戳 (毫秒)
} local_sensor_data_t;

// ==================== 扩展传感器接口 ====================

/**
 * @brief 传感器ID枚举 (用于扩展传感器)
 */
typedef enum {
    SENSOR_ID_CUSTOM_1 = 0x10,
    SENSOR_ID_CUSTOM_2 = 0x11,
    SENSOR_ID_CUSTOM_3 = 0x12,
    // 可继续扩展...
} sensor_id_t;

/**
 * @brief 自定义传感器读取函数类型
 * @param user_data 用户自定义数据
 * @param output_data 输出数据缓冲区
 * @param output_size 输出数据大小
 * @return ESP_OK表示成功，其他表示失败
 */
typedef esp_err_t (*sensor_read_func_t)(void *user_data, void *output_data, size_t output_size);

// ==================== 核心API ====================

/**
 * @brief 初始化遥测数据转换器
 * @return ESP_OK表示成功
 */
esp_err_t telemetry_data_converter_init(void);

/**
 * @brief 更新所有传感器数据
 * @return ESP_OK表示成功
 */
esp_err_t telemetry_data_converter_update(void);

/**
 * @brief 获取遥控通道数据
 * @param channels 输出通道数组 (至少8个元素)
 * @param channel_count 输出通道数量
 * @return ESP_OK表示成功
 */
esp_err_t telemetry_data_converter_get_rc_channels(uint16_t *channels, uint8_t *channel_count);

/**
 * @brief 获取遥测数据
 * @param telemetry 输出遥测数据结构
 * @return ESP_OK表示成功
 */
esp_err_t telemetry_data_converter_get_telemetry_data(telemetry_data_t *telemetry);

/**
 * @brief 获取本地传感器原始数据
 * @param sensor_data 输出传感器数据结构
 * @return ESP_OK表示成功
 */
esp_err_t telemetry_data_converter_get_sensor_data(local_sensor_data_t *sensor_data);

/**
 * @brief 检查数据是否有效
 * @return true表示数据有效，false表示无效
 */
bool telemetry_data_converter_is_data_valid(void);

/**
 * @brief 获取设备状态
 * @param status 输出设备状态 (0x00=空闲, 0x01=正常, 0x02=错误)
 * @return ESP_OK表示成功
 */
esp_err_t telemetry_data_converter_get_device_status(uint8_t *status);
                     
// ==================== ELRS特定API ====================

/**
 * @brief 更新ELRS链路统计数据
 * @param rssi_1 天线1 RSSI (dBm + 120偏移)
 * @param rssi_2 天线2 RSSI (dBm + 120偏移)
 * @param link_quality 链路质量 (0-100%)
 * @param snr 信噪比 (dB)
 * @return ESP_OK表示成功
 */
esp_err_t telemetry_data_converter_update_link_stats(uint8_t rssi_1, uint8_t rssi_2,
                                                     uint8_t link_quality, int8_t snr);

/**
 * @brief 更新ELRS遥控通道数据 (16个CRSF格式通道)
 * @param channels 16个通道数据数组 (0-2047)
 * @return ESP_OK表示成功
 */
esp_err_t telemetry_data_converter_update_channels(const uint16_t *channels);

/**
 * @brief 更新ELRS天线和模型匹配信息
 * @param antenna 当前天线 (0或1)
 * @param model_match 模型是否匹配
 * @param diversity_available 双天线是否可用
 * @return ESP_OK表示成功
 */
esp_err_t telemetry_data_converter_update_antenna_info(uint8_t antenna, bool model_match, bool diversity_available);

/**
 * @brief 获取当前ELRS链路统计数据
 * @param stats 输出链路统计数据
 * @return ESP_OK表示成功
 */
esp_err_t telemetry_data_converter_get_link_stats(elrs_link_stats_t *stats);

/**
 * @brief 获取当前ELRS遥控通道数据
 * @param channels 输出16个通道数据 (0-2047)
 * @return ESP_OK表示成功
 */
esp_err_t telemetry_data_converter_get_channels(uint16_t *channels);

#ifdef __cplusplus
}
#endif

#endif // TELEMETRY_DATA_CONVERTER_H
