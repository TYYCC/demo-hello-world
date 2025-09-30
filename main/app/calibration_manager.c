/**
 * @file calibration_manager.c
 * @brief 外设校准管理器 - 管理各种外设的校准数据和NVS存储
 * @author Your Name
 * @date 2024
 */
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "calibration_manager.h"
#include "joystick_adc.h"
#include "lsm6ds3.h"

static const char *TAG = "CALIBRATION_MANAGER";

// NVS命名空间
#define NVS_NAMESPACE "calibration"

// 校准数据结构 - 存储在PSRAM中
typedef struct {
    // 摇杆校准数据
    struct {
        int16_t center_x;
        int16_t center_y;
        int16_t min_x;
        int16_t max_x;
        int16_t min_y;
        int16_t max_y;
        float deadzone;
        bool calibrated;
    } joystick;
    
    // 陀螺仪校准数据
    struct {
        float bias_x;
        float bias_y;
        float bias_z;
        float scale_x;
        float scale_y;
        float scale_z;
        bool calibrated;
    } gyroscope;
    
    // 加速度计校准数据
    struct {
        float bias_x;
        float bias_y;
        float bias_z;
        float scale_x;
        float scale_y;
        float scale_z;
        bool calibrated;
    } accelerometer;
    
    // 电池校准数据
    struct {
        float voltage_scale;
        float voltage_offset;
        bool calibrated;
    } battery;
    
    // 触摸屏校准数据
    struct {
        float matrix[6];  // 3x2变换矩阵
        bool calibrated;
    } touchscreen;
    
} calibration_data_t;

// 全局校准数据 - 存储在PSRAM
static calibration_data_t *g_calibration_data = NULL;
static bool g_initialized = false;

// 校准状态
static calibration_status_t g_calibration_status = {
    .joystick_calibrated = false,
    .gyroscope_calibrated = false,
    .accelerometer_calibrated = false,
    .battery_calibrated = false,
    .touchscreen_calibrated = false
};

// 初始化PSRAM校准数据
static esp_err_t init_psram_calibration_data(void)
{
    if (g_calibration_data != NULL) {
        return ESP_OK;
    }
    
    // 分配PSRAM内存
    g_calibration_data = (calibration_data_t *)heap_caps_malloc(
        sizeof(calibration_data_t), MALLOC_CAP_SPIRAM);
    
    if (g_calibration_data == NULL) {
        ESP_LOGE(TAG, "Failed to allocate PSRAM for calibration data");
        return ESP_ERR_NO_MEM;
    }
    
    // 初始化数据结构
    memset(g_calibration_data, 0, sizeof(calibration_data_t));
    
    ESP_LOGI(TAG, "PSRAM calibration data initialized: %d bytes", 
             sizeof(calibration_data_t));
    return ESP_OK;
}

// 清理PSRAM校准数据
static void cleanup_psram_calibration_data(void)
{
    if (g_calibration_data != NULL) {
        heap_caps_free(g_calibration_data);
        g_calibration_data = NULL;
    }
}

// 从NVS加载校准数据
static esp_err_t load_calibration_from_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;
    
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return err;
    }
    
    size_t required_size = sizeof(calibration_data_t);
    // NVS key 最大长度15字符，使用缩写
    err = nvs_get_blob(nvs_handle, "calib_data", g_calibration_data, &required_size);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Calibration data loaded from NVS (size: %d bytes)", required_size);
        
        // 验证数据完整性
        if (required_size != sizeof(calibration_data_t)) {
            ESP_LOGW(TAG, "Calibration data size mismatch! Expected %d, got %d", 
                     sizeof(calibration_data_t), required_size);
            nvs_close(nvs_handle);
            return ESP_ERR_INVALID_SIZE;
        }
        
        // 更新校准状态
        g_calibration_status.joystick_calibrated = g_calibration_data->joystick.calibrated;
        g_calibration_status.gyroscope_calibrated = g_calibration_data->gyroscope.calibrated;
        g_calibration_status.accelerometer_calibrated = g_calibration_data->accelerometer.calibrated;
        g_calibration_status.battery_calibrated = g_calibration_data->battery.calibrated;
        g_calibration_status.touchscreen_calibrated = g_calibration_data->touchscreen.calibrated;
        
        // 打印详细的校准状态
        ESP_LOGI(TAG, "Calibration status loaded:");
        ESP_LOGI(TAG, "  - Joystick: %s", g_calibration_status.joystick_calibrated ? "Calibrated" : "Not calibrated");
        ESP_LOGI(TAG, "  - Gyroscope: %s (bias: %.3f, %.3f, %.3f)", 
                 g_calibration_status.gyroscope_calibrated ? "Calibrated" : "Not calibrated",
                 g_calibration_data->gyroscope.bias_x,
                 g_calibration_data->gyroscope.bias_y,
                 g_calibration_data->gyroscope.bias_z);
        ESP_LOGI(TAG, "  - Accelerometer: %s", g_calibration_status.accelerometer_calibrated ? "Calibrated" : "Not calibrated");
        ESP_LOGI(TAG, "  - Battery: %s", g_calibration_status.battery_calibrated ? "Calibrated" : "Not calibrated");
        ESP_LOGI(TAG, "  - Touchscreen: %s", g_calibration_status.touchscreen_calibrated ? "Calibrated" : "Not calibrated");
    } else {
        ESP_LOGW(TAG, "No calibration data found in NVS: %s", esp_err_to_name(err));
        
        // 确保状态为未校准
        g_calibration_status.joystick_calibrated = false;
        g_calibration_status.gyroscope_calibrated = false;
        g_calibration_status.accelerometer_calibrated = false;
        g_calibration_status.battery_calibrated = false;
        g_calibration_status.touchscreen_calibrated = false;
    }
    
    nvs_close(nvs_handle);
    return err;
}

// 保存校准数据到NVS
static esp_err_t save_calibration_to_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;
    
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return err;
    }
    
    // NVS key 最大长度15字符，使用缩写
    err = nvs_set_blob(nvs_handle, "calib_data", g_calibration_data, sizeof(calibration_data_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save calibration data: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit calibration data: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Calibration data saved to NVS (%d bytes)", sizeof(calibration_data_t));
        ESP_LOGI(TAG, "Saved status: Gyro=%d, Accel=%d, Joystick=%d",
                 g_calibration_data->gyroscope.calibrated,
                 g_calibration_data->accelerometer.calibrated,
                 g_calibration_data->joystick.calibrated);
    }
    
    nvs_close(nvs_handle);
    return err;
}

// 公共API函数

esp_err_t calibration_manager_init(void)
{
    if (g_initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing calibration manager...");
    
    // 初始化PSRAM校准数据
    esp_err_t ret = init_psram_calibration_data();
    if (ret != ESP_OK) {
        return ret;
    }
    
    // 从NVS加载校准数据
    load_calibration_from_nvs();
    
    g_initialized = true;
    ESP_LOGI(TAG, "Calibration manager initialized successfully");
    return ESP_OK;
}

void calibration_manager_deinit(void)
{
    if (!g_initialized) {
        return;
    }
    
    // 保存校准数据到NVS
    save_calibration_to_nvs();
    
    // 清理PSRAM数据
    cleanup_psram_calibration_data();
    
    g_initialized = false;
    ESP_LOGI(TAG, "Calibration manager deinitialized");
}

// 摇杆校准函数
esp_err_t calibrate_joystick(void)
{
    if (!g_initialized || g_calibration_data == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Starting joystick calibration...");
    
    // 读取当前摇杆值作为中心点
    joystick_data_t joystick_data;
    if (joystick_adc_read(&joystick_data) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read joystick data");
        return ESP_FAIL;
    }
    
    // 设置中心点
    g_calibration_data->joystick.center_x = joystick_data.norm_joy1_x;
    g_calibration_data->joystick.center_y = joystick_data.norm_joy1_y;
    
    // 初始化范围值
    g_calibration_data->joystick.min_x = joystick_data.norm_joy1_x;
    g_calibration_data->joystick.max_x = joystick_data.norm_joy1_x;
    g_calibration_data->joystick.min_y = joystick_data.norm_joy1_y;
    g_calibration_data->joystick.max_y = joystick_data.norm_joy1_y;
    
    // 设置死区
    g_calibration_data->joystick.deadzone = 0.1f; // 10%死区
    
    // 标记为已校准
    g_calibration_data->joystick.calibrated = true;
    g_calibration_status.joystick_calibrated = true;
    
    // 保存到NVS
    esp_err_t ret = save_calibration_to_nvs();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save joystick calibration to NVS");
    }
    
    ESP_LOGI(TAG, "Joystick calibrated - Center: (%d, %d)", 
             g_calibration_data->joystick.center_x, g_calibration_data->joystick.center_y);
    
    return ESP_OK;
}

// 陀螺仪校准函数
esp_err_t calibrate_gyroscope(void)
{
    if (!g_initialized || g_calibration_data == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Starting gyroscope calibration... Please keep the device STILL!");
    
    // 增加采样数量和时间，提高校准精度
    const int samples = 500;  // 从100增加到500
    const int delay_ms = 10;  // 10ms间隔 = 总共5秒
    float sum_x = 0, sum_y = 0, sum_z = 0;
    
    // 第一阶段：检测设备是否静止
    float initial_x = 0, initial_y = 0, initial_z = 0;
    bool first_read = true;
    
    for (int i = 0; i < samples; i++) {
        lsm6ds3_data_t imu_data;
        if (lsm6ds3_read_all(&imu_data) == ESP_OK) {
            // 第一次读取作为基准
            if (first_read) {
                initial_x = imu_data.gyro.x;
                initial_y = imu_data.gyro.y;
                initial_z = imu_data.gyro.z;
                first_read = false;
            }
            
            // 检测运动：如果变化超过阈值，说明设备在移动
            float delta_x = imu_data.gyro.x - initial_x;
            float delta_y = imu_data.gyro.y - initial_y;
            float delta_z = imu_data.gyro.z - initial_z;
            float motion = sqrtf(delta_x*delta_x + delta_y*delta_y + delta_z*delta_z);
            
            if (motion > 5.0f) {  // 阈值：5 dps
                ESP_LOGW(TAG, "Device is moving! Please keep still. Restarting calibration...");
                // 重置采样
                i = 0;
                sum_x = sum_y = sum_z = 0;
                first_read = true;
                vTaskDelay(pdMS_TO_TICKS(500)); // 等待500ms后重新开始
                continue;
            }
            
            sum_x += imu_data.gyro.x;
            sum_y += imu_data.gyro.y;
            sum_z += imu_data.gyro.z;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    
    // 计算平均偏置
    g_calibration_data->gyroscope.bias_x = sum_x / samples;
    g_calibration_data->gyroscope.bias_y = sum_y / samples;
    g_calibration_data->gyroscope.bias_z = sum_z / samples;
    
    // 设置默认比例因子
    g_calibration_data->gyroscope.scale_x = 1.0f;
    g_calibration_data->gyroscope.scale_y = 1.0f;
    g_calibration_data->gyroscope.scale_z = 1.0f;
    
    // 标记为已校准
    g_calibration_data->gyroscope.calibrated = true;
    g_calibration_status.gyroscope_calibrated = true;
    
    // 保存到NVS
    save_calibration_to_nvs();
    
    ESP_LOGI(TAG, "Gyroscope calibrated successfully!");
    ESP_LOGI(TAG, "Bias: X=%.3f, Y=%.3f, Z=%.3f dps", 
             g_calibration_data->gyroscope.bias_x,
             g_calibration_data->gyroscope.bias_y,
             g_calibration_data->gyroscope.bias_z);
    
    return ESP_OK;
}

// 加速度计校准函数
esp_err_t calibrate_accelerometer(void)
{
    if (!g_initialized || g_calibration_data == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Starting accelerometer calibration...");
    ESP_LOGI(TAG, "Please place device on a FLAT, LEVEL surface!");
    
    // 增加采样数量和时间
    const int samples = 500;  // 从100增加到500
    const int delay_ms = 10;  // 10ms间隔 = 总共5秒
    float sum_x = 0, sum_y = 0, sum_z = 0;
    
    // 静止检测
    float initial_x = 0, initial_y = 0, initial_z = 0;
    bool first_read = true;
    
    for (int i = 0; i < samples; i++) {
        lsm6ds3_data_t imu_data;
        if (lsm6ds3_read_all(&imu_data) == ESP_OK) {
            if (first_read) {
                initial_x = imu_data.accel.x;
                initial_y = imu_data.accel.y;
                initial_z = imu_data.accel.z;
                first_read = false;
            }
            
            // 检测运动：加速度计阈值0.1g
            float delta_x = imu_data.accel.x - initial_x;
            float delta_y = imu_data.accel.y - initial_y;
            float delta_z = imu_data.accel.z - initial_z;
            float motion = sqrtf(delta_x*delta_x + delta_y*delta_y + delta_z*delta_z);
            
            if (motion > 0.1f) {  // 阈值：0.1g
                ESP_LOGW(TAG, "Device is moving! Please keep still. Restarting calibration...");
                i = 0;
                sum_x = sum_y = sum_z = 0;
                first_read = true;
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }
            
            sum_x += imu_data.accel.x;
            sum_y += imu_data.accel.y;
            sum_z += imu_data.accel.z;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    
    // 计算平均值
    float avg_x = sum_x / samples;
    float avg_y = sum_y / samples;
    float avg_z = sum_z / samples;
    
    // 计算偏置（假设设备水平放置，Z轴应该是1g）
    g_calibration_data->accelerometer.bias_x = avg_x;
    g_calibration_data->accelerometer.bias_y = avg_y;
    g_calibration_data->accelerometer.bias_z = avg_z - 1.0f; // 减去1g重力（单位是g）
    
    // 设置默认比例因子
    g_calibration_data->accelerometer.scale_x = 1.0f;
    g_calibration_data->accelerometer.scale_y = 1.0f;
    g_calibration_data->accelerometer.scale_z = 1.0f;
    
    // 标记为已校准
    g_calibration_data->accelerometer.calibrated = true;
    g_calibration_status.accelerometer_calibrated = true;
    
    // 保存到NVS
    save_calibration_to_nvs();
    
    ESP_LOGI(TAG, "Accelerometer calibrated successfully!");
    ESP_LOGI(TAG, "Bias: X=%.3f, Y=%.3f, Z=%.3f g", 
             g_calibration_data->accelerometer.bias_x,
             g_calibration_data->accelerometer.bias_y,
             g_calibration_data->accelerometer.bias_z);
    
    return ESP_OK;
}

// 获取校准状态
const calibration_status_t* get_calibration_status(void)
{
    return &g_calibration_status;
}

// 获取摇杆校准数据
const joystick_calibration_t* get_joystick_calibration(void)
{
    if (!g_initialized || g_calibration_data == NULL) {
        return NULL;
    }
    
    static joystick_calibration_t cal_data;
    cal_data.center_x = g_calibration_data->joystick.center_x;
    cal_data.center_y = g_calibration_data->joystick.center_y;
    cal_data.min_x = g_calibration_data->joystick.min_x;
    cal_data.max_x = g_calibration_data->joystick.max_x;
    cal_data.min_y = g_calibration_data->joystick.min_y;
    cal_data.max_y = g_calibration_data->joystick.max_y;
    cal_data.deadzone = g_calibration_data->joystick.deadzone;
    cal_data.calibrated = g_calibration_data->joystick.calibrated;
    
    return &cal_data;
}

// 获取陀螺仪校准数据
const gyroscope_calibration_t* get_gyroscope_calibration(void)
{
    if (!g_initialized || g_calibration_data == NULL) {
        return NULL;
    }
    
    static gyroscope_calibration_t cal_data;
    cal_data.bias_x = g_calibration_data->gyroscope.bias_x;
    cal_data.bias_y = g_calibration_data->gyroscope.bias_y;
    cal_data.bias_z = g_calibration_data->gyroscope.bias_z;
    cal_data.scale_x = g_calibration_data->gyroscope.scale_x;
    cal_data.scale_y = g_calibration_data->gyroscope.scale_y;
    cal_data.scale_z = g_calibration_data->gyroscope.scale_z;
    cal_data.calibrated = g_calibration_data->gyroscope.calibrated;
    
    return &cal_data;
}

// 获取加速度计校准数据
const accelerometer_calibration_t* get_accelerometer_calibration(void)
{
    if (!g_initialized || g_calibration_data == NULL) {
        return NULL;
    }
    
    static accelerometer_calibration_t cal_data;
    cal_data.bias_x = g_calibration_data->accelerometer.bias_x;
    cal_data.bias_y = g_calibration_data->accelerometer.bias_y;
    cal_data.bias_z = g_calibration_data->accelerometer.bias_z;
    cal_data.scale_x = g_calibration_data->accelerometer.scale_x;
    cal_data.scale_y = g_calibration_data->accelerometer.scale_y;
    cal_data.scale_z = g_calibration_data->accelerometer.scale_z;
    cal_data.calibrated = g_calibration_data->accelerometer.calibrated;
    
    return &cal_data;
}

// 应用摇杆校准
esp_err_t apply_joystick_calibration(joystick_data_t *data)
{
    if (!g_initialized || g_calibration_data == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!g_calibration_data->joystick.calibrated) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // 应用中心点偏移
    int16_t calibrated_x = data->norm_joy1_x - g_calibration_data->joystick.center_x;
    int16_t calibrated_y = data->norm_joy1_y - g_calibration_data->joystick.center_y;
    
    // 应用死区
    float deadzone = g_calibration_data->joystick.deadzone;
    int16_t max_range = 2048; // 假设12位ADC
    int16_t deadzone_threshold = (int16_t)(max_range * deadzone);
    
    if (abs(calibrated_x) < deadzone_threshold) {
        calibrated_x = 0;
    }
    if (abs(calibrated_y) < deadzone_threshold) {
        calibrated_y = 0;
    }
    
    data->norm_joy1_x = calibrated_x;
    data->norm_joy1_y = calibrated_y;
    
    return ESP_OK;
}

// 应用陀螺仪校准
esp_err_t apply_gyroscope_calibration(float *gyro_x, float *gyro_y, float *gyro_z)
{
    if (!g_initialized || g_calibration_data == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!g_calibration_data->gyroscope.calibrated) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (gyro_x) {
        *gyro_x = (*gyro_x - g_calibration_data->gyroscope.bias_x) * g_calibration_data->gyroscope.scale_x;
    }
    if (gyro_y) {
        *gyro_y = (*gyro_y - g_calibration_data->gyroscope.bias_y) * g_calibration_data->gyroscope.scale_y;
    }
    if (gyro_z) {
        *gyro_z = (*gyro_z - g_calibration_data->gyroscope.bias_z) * g_calibration_data->gyroscope.scale_z;
    }
    
    return ESP_OK;
}

// 应用加速度计校准
esp_err_t apply_accelerometer_calibration(float *accel_x, float *accel_y, float *accel_z)
{
    if (!g_initialized || g_calibration_data == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!g_calibration_data->accelerometer.calibrated) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (accel_x) {
        *accel_x = (*accel_x - g_calibration_data->accelerometer.bias_x) * g_calibration_data->accelerometer.scale_x;
    }
    if (accel_y) {
        *accel_y = (*accel_y - g_calibration_data->accelerometer.bias_y) * g_calibration_data->accelerometer.scale_y;
    }
    if (accel_z) {
        *accel_z = (*accel_z - g_calibration_data->accelerometer.bias_z) * g_calibration_data->accelerometer.scale_z;
    }
    
    return ESP_OK;
}
