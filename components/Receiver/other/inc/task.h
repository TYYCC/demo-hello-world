/*
 * @Author: tidycraze 2595256284@qq.com
 * @Date: 2025-09-05 10:04:45
 * @LastEditors: tidycraze 2595256284@qq.com
 * @LastEditTime: 2025-09-05 11:13:06
 * @FilePath: \demo-hello-world\components\Receiver\inc\task.h
 * @Description: 任务头文件
 * 
 */
#pragma once

#include "esp_event.h"
#include "wifi_pairing_manager.h"

/**
 * @brief 初始化TCP任务管理器
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t tcp_task_manager_init(void);

/**
 * @brief 启动TCP任务管理器
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t tcp_task_manager_start(void);

/**
 * @brief 启动带WIFI事件集成的TCP任务管理器
 * @param wifi_config WIFI配对配置
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t tcp_task_manager_start_with_wifi(const wifi_pairing_config_t* wifi_config);

/**
 * @brief 停止TCP任务管理器
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t tcp_task_manager_stop(void);

/**
 * @brief 传统的TCP任务函数（保持向后兼容）
 */
void tcp_task(void);