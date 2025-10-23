/*
 * @Author: tidycraze 2595256284@qq.com
 * @Date: 2025-08-22 16:32:04
 * @LastEditors: tidycraze 2595256284@qq.com
 * @LastEditTime: 2025-08-28 14:36:28
 * @FilePath: \demo-hello-world\main\components_init.c
 * @Description: 用于集中初始化必要外设
 *
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved.
 */

#include <dirent.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"

#include "battery_monitor.h"
#include "bsp_i2c.h"
#include "calibration_manager.h"
#include "gt911.h"
#include "ft6336g.h"
#include "lsm6ds3.h"
#include "lv_port_indev.h" // 包含此头文件以获取宏定义
#include "settings_manager.h"
#include "status_bar_manager.h"
#include "ui_state_manager.h"
#include "task_init.h"
#include "ui.h"

static const char* TAG = "COMPONENTS_INIT";

/**
 * @brief 初始化SPIFFS文件系统
 * @return esp_err_t ESP_OK成功，其他错误码失败
 */
esp_err_t spiffs_init(void) {
    ESP_LOGI(TAG, "Initializing SPIFFS");

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs", .partition_label = "spiffs", .max_files = 5, .format_if_mount_failed = false};

    // 挂载SPIFFS分区
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    // 获取SPIFFS分区信息
    size_t total = 0, used = 0;
    ret = esp_spiffs_info("spiffs", &total, &used);
    if (ret != ESP_OK) {
        esp_vfs_spiffs_unregister("spiffs");
        return ret;
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    // 检查SPIFFS分区是否正常工作
    if (used > total) {
        ESP_LOGW(TAG, "Number of used bytes cannot be larger than total. Performing SPIFFS check.");
        ret = esp_spiffs_check("spiffs");
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SPIFFS check failed (%s)", esp_err_to_name(ret));
            return ret;
        } else {
            ESP_LOGI(TAG, "SPIFFS check successful");
        }
    }

    ESP_LOGI(TAG, "SPIFFS mounted successfully");
    return ESP_OK;
}

/**
 * @brief 卸载SPIFFS文件系统
 */
void spiffs_deinit(void) {
    esp_vfs_spiffs_unregister("spiffs");
    ESP_LOGI(TAG, "SPIFFS unmounted");
}

/**
 * @brief 初始化所有必要组件
 * @return esp_err_t ESP_OK成功，其他错误码失败
 */
esp_err_t components_init(void) {
    esp_err_t ret;

    // 初始化NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 初始化I2C总线
    ret = bsp_i2c_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C bus");
        return ret;
    }

    // 初始化SPIFFS文件系统
    ret = spiffs_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPIFFS");
        return ret;
    }

    // 初始化UI状态管理器
    ui_state_manager_init();
    ESP_LOGI(TAG, "UI state manager initialized");

    // 初始化设置管理器
    settings_manager_init();
    ESP_LOGI(TAG, "Settings manager initialized.");

    // 初始化状态栏管理器
    ret = status_bar_manager_init();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Status bar manager initialized");
    } else {
        ESP_LOGW(TAG, "Status bar manager init failed: %s", esp_err_to_name(ret));
    }

    // 初始化LVGL任务,用于实时显示加载动画
    ret = init_lvgl_task();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init LVGL task");
        return ret;
    }
    ui_start_animation_update_state(UI_STAGE_INITIALIZING);
    ui_start_animation_set_progress((float)1 / UI_STAGE_DONE * 100);

    // 初始化校准管理器
    ret = calibration_manager_init();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration manager initialized");
    } else {
        ESP_LOGW(TAG, "Calibration manager init failed: %s", esp_err_to_name(ret));
    }
    ui_start_animation_update_state(UI_STAGE_LOADING_COMPONENTS);
    ui_start_animation_set_progress((float)2 / UI_STAGE_DONE * 100);

    // 初始化电池监测模块
    ret = battery_monitor_init();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Battery monitor initialized");
    } else {
        ESP_LOGW(TAG, "Battery monitor init failed: %s", esp_err_to_name(ret));
    }
    ui_start_animation_update_state(UI_STAGE_CONFIGURING_HARDWARE);
    ui_start_animation_set_progress((float)3 / UI_STAGE_DONE * 100);

    // 初始化LSM6DS3传感器
    ret = lsm6ds3_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LSM6DS3 initialization failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "LSM6DS3 initialized successfully");
    }

    // 初始化FT6336G触摸控制器
    ret = ft6336g_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FT6336G initialization failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "FT6336G initialized successfully");
    }

    // 其他组件初始化可以在这里添加

    ESP_LOGI(TAG, "All components initialized successfully");
    return ESP_OK;
}