/**
 * @file telemetry_data_converter.c
 * @brief 本地传感器数据获取和转换模块
 * @author TidyCraze
 * @date 2025-12-10
 */

#include "telemetry_data_converter.h"
#include "telemetry_protocol.h"
#include "joystick_adc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "telemetry_converter";

// 内部数据缓存
static local_sensor_data_t s_cached_data = {0};
static bool s_data_valid = false;

/**
 * @brief 将摇杆原始值(-100~100)转换为遥控通道值(0~1000)
 */
static uint16_t convert_joystick_to_channel(int16_t joystick_value) {
    // 摇杆值范围: -100 ~ 100
    // 遥控通道值范围: 0 ~ 1000 (500为中位)
    
    // 限制输入范围
    if (joystick_value < -100) joystick_value = -100;
    if (joystick_value > 100) joystick_value = 100;
    
    // 转换: -100->0, 0->500, 100->1000
    return (uint16_t)((joystick_value + 100) * 500 / 100);
}

/**
 * @brief 将角度值转换为遥测数据格式(0.01度单位)
 */
static int16_t convert_angle_to_telemetry(float angle_deg) {
    return (int16_t)(angle_deg * 100.0f);
}

esp_err_t telemetry_data_converter_init(void) {
    ESP_LOGI(TAG, "Initializing telemetry data converter");
    
    // 清空缓存数据
    memset(&s_cached_data, 0, sizeof(s_cached_data));
    s_data_valid = false;
    
    ESP_LOGI(TAG, "Telemetry data converter initialized");
    return ESP_OK;
}

esp_err_t telemetry_data_converter_update(void) {
    esp_err_t ret = ESP_OK;
    
    // 1. 获取摇杆数据
    joystick_data_t joystick_data;
    if (joystick_adc_read(&joystick_data) == ESP_OK) {
        s_cached_data.joystick.joy_x = joystick_data.norm_joy1_x;
        s_cached_data.joystick.joy_y = joystick_data.norm_joy1_y;
        s_cached_data.joystick.valid = true;
        
    } else {
        s_cached_data.joystick.valid = false;
        ESP_LOGW(TAG, "Failed to read joystick data");
        ret = ESP_FAIL;
    }
    
    // 2. 获取IMU数据 (可选功能)
#ifdef CONFIG_ENABLE_IMU_SENSOR
    lsm6ds3_data_t imu_data;
    if (lsm6ds3_read_all(&imu_data) == ESP_OK) {
        s_cached_data.imu.roll = imu_data.accel.x;  // 根据实际轴向调整
        s_cached_data.imu.pitch = imu_data.accel.y;
        s_cached_data.imu.yaw = imu_data.gyro.z;
        s_cached_data.imu.valid = true;
        
    } else {
        s_cached_data.imu.valid = false;
        ESP_LOGW(TAG, "Failed to read IMU data");
    }
#else
    // IMU功能未启用，使用默认值
    s_cached_data.imu.roll = 0.0f;
    s_cached_data.imu.pitch = 0.0f;
    s_cached_data.imu.yaw = 0.0f;
    s_cached_data.imu.valid = false;
    // IMU传感器未启用
#endif
    
    // 4. 更新时间戳
    s_cached_data.timestamp_ms = esp_timer_get_time() / 1000;
    s_data_valid = true;
    
    return ret;
}

esp_err_t telemetry_data_converter_get_rc_channels(uint16_t *channels, uint8_t *channel_count) {
    if (!channels || !channel_count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_data_valid || !s_cached_data.joystick.valid) {
        ESP_LOGW(TAG, "Joystick data not available");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 根据协议文档:
    // CH1: 油门 (摇杆Y轴)
    // CH2: 方向 (摇杆X轴)
    
    channels[0] = convert_joystick_to_channel(s_cached_data.joystick.joy_y);  // 油门
    channels[1] = convert_joystick_to_channel(s_cached_data.joystick.joy_x);  // 方向
    
    // 预留其他通道，设为中位值
    channels[2] = 500;  // 预留通道3
    channels[3] = 500;  // 预留通道4
    
    *channel_count = 4;  // 目前使用4个通道
    
    // RC通道数据转换
    
    return ESP_OK;
}

esp_err_t telemetry_data_converter_get_telemetry_data(telemetry_data_t *telemetry) {
    if (!telemetry) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_data_valid) {
        ESP_LOGW(TAG, "Sensor data not available");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 填充ELRS扩展遥测数据
    if (s_cached_data.battery.valid) {
        telemetry->voltage = s_cached_data.battery.voltage_mv / 1000.0f;
        telemetry->current = s_cached_data.battery.current_ma / 1000.0f;
    } else {
        telemetry->voltage = 0;
        telemetry->current = 0;
    }
    
    if (s_cached_data.imu.valid) {
        telemetry->roll = s_cached_data.imu.roll;
        telemetry->pitch = s_cached_data.imu.pitch;
        telemetry->yaw = s_cached_data.imu.yaw;
    } else {
        telemetry->roll = 0;
        telemetry->pitch = 0;
        telemetry->yaw = 0;
    }
    
    // 高度数据 - 目前暂无传感器，设为0
    telemetry->altitude = 0;
    
    // 遥测数据打包完成
    
    return ESP_OK;
}

esp_err_t telemetry_data_converter_get_sensor_data(local_sensor_data_t *sensor_data) {
    if (!sensor_data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_data_valid) {
        ESP_LOGW(TAG, "Sensor data not available");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 复制缓存的传感器数据
    memcpy(sensor_data, &s_cached_data, sizeof(local_sensor_data_t));
    
    return ESP_OK;
}

bool telemetry_data_converter_is_data_valid(void) {
    return s_data_valid;
}

esp_err_t telemetry_data_converter_get_device_status(uint8_t *status) {
    if (!status) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_data_valid) {
        *status = 0x02;  // 错误状态
    } else if (s_cached_data.joystick.valid && s_cached_data.imu.valid) {
        *status = 0x01;  // 正常运行
    } else {
        *status = 0x00;  // 空闲状态
    }
    
    return ESP_OK;
}
