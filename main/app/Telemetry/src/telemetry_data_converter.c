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
 * @brief 将摇杆原始值(-100~100)转换为ELRS CRSF通道值(172~1811)
 * CRSF范围: 172-1811, 中位: 992
 * 摇杆范围: -100 ~ 100, 中位: 0
 */
static uint16_t convert_joystick_to_channel(int16_t joystick_value) {
    // 摇杆值范围: -100 ~ 100
    // CRSF通道值范围: 172 ~ 1811 (1639个单位), 中位: 992
    
    // 限制输入范围
    if (joystick_value < -100) joystick_value = -100;
    if (joystick_value > 100) joystick_value = 100;
    
    return (uint16_t)(992 + (joystick_value * 1639) / 200);
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
    
    channels[0] = convert_joystick_to_channel(s_cached_data.joystick.joy_y);  // 油门
    channels[1] = convert_joystick_to_channel(s_cached_data.joystick.joy_x);  // 方向
    
    // RC通道数据转换
    
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
