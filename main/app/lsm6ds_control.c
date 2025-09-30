/*
 * @Author: tidycraze 2595256284@qq.com
 * @Date: 2025-08-28 14:10:35
 * @LastEditors: tidycraze 2595256284@qq.com
 * @LastEditTime: 2025-08-28 14:36:21
 * @FilePath: \demo-hello-world\main\app\lsm6ds_control.c
 * @Description: 陀螺仪控制任务
 *
 */
#include "lsm6ds_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include "calibration_manager.h"  // 需要应用校准数据


static char* TAG = "LSM6DS3_CTRL";

static float pitch = 0.0f;
static float roll = 0.0f;
static float yaw = 0.0f;
static SemaphoreHandle_t attitude_mutex = NULL;

TaskHandle_t s_lsm6ds3_control_task = NULL;

// 如需零偏校准，可在此添加调用；Fusion 已能一定程度抑制误差

static void lsm6ds3_control_task(void* pvParameters) {
    esp_err_t ret;

    // 配置加速度计: 416Hz, ±2g（提高采样率）
    ret = lsm6ds3_config_accel(LSM6DS3_ODR_416_HZ, LSM6DS3_ACCEL_FS_2G);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure accelerometer");
        lsm6ds3_deinit();
        vTaskDelete(NULL);
    }

    // 配置陀螺仪: 416Hz, ±250dps（提高采样率）
    ret = lsm6ds3_config_gyro(LSM6DS3_ODR_416_HZ, LSM6DS3_GYRO_FS_250DPS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure gyroscope");
        lsm6ds3_deinit();
        vTaskDelete(NULL);
    }

    // 启用传感器
    lsm6ds3_accel_enable(true);
    lsm6ds3_gyro_enable(true);

    ESP_LOGI(TAG, "LSM6DS3 control task started, using Fusion AHRS");
    ESP_LOGI(TAG, "Waiting for initial calibration to settle...");
    
    // 等待 2 秒让传感器稳定
    vTaskDelay(pdMS_TO_TICKS(2000));

    while (1) {
        // 【关键修改】使用 lsm6ds3_read_euler()，它内部已经包含：
        // 1. 读取原始数据
        // 2. Fusion AHRS 处理
        // 3. FusionOffset 零漂补偿
        lsm6ds3_euler_t e = {0};
        ret = lsm6ds3_read_euler(&e);
        if (ret == ESP_OK) {
            if (xSemaphoreTake(attitude_mutex, portMAX_DELAY) == pdTRUE) {
                pitch = e.pitch;
                roll = e.roll;
                yaw = e.yaw;
                xSemaphoreGive(attitude_mutex);
            }
        } else {
            ESP_LOGW(TAG, "Failed to read euler angles");
        }
        
        // 50Hz 更新频率（从 10Hz 提高到 50Hz，让 Fusion 更稳定）
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void lsm6ds_control_get_attitude(attitude_data_t* data) {
    if (data != NULL && xSemaphoreTake(attitude_mutex, portMAX_DELAY) == pdTRUE) {
        data->pitch = pitch;
        data->roll = roll;
        data->yaw = yaw;
        xSemaphoreGive(attitude_mutex);
    }
}

esp_err_t init_lsm6ds3_control_task(void) {
    if (attitude_mutex == NULL) {
        attitude_mutex = xSemaphoreCreateMutex();
        if (attitude_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create attitude mutex");
            return ESP_FAIL;
        }
    }

    if (s_lsm6ds3_control_task != NULL) {
        ESP_LOGW(TAG, "LSM6DS3 control task already running");
        return ESP_OK;
    }
    // 创建LSM6DS3控制任务
    BaseType_t result = xTaskCreatePinnedToCore(lsm6ds3_control_task, "lsm6ds3_control", 4096, NULL,
                                                5, &s_lsm6ds3_control_task, 0);

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LSM6DS3 control task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "LSM6DS3 control task created successfully on Core 0");
    return ESP_OK;
}

TaskHandle_t get_lsm6ds3_control_task_handle(void) { return s_lsm6ds3_control_task; }
