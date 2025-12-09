/**
 * @file ui_event_queue.c
 * @brief LVGL线程安全的UI事件队列实现
 * @author GitHub Copilot
 * @date 2025-12-09
 */

#include "ui_event_queue.h"
#include "esp_log.h"
#include "background_manager.h"
#include "status_bar_manager.h"
#include <string.h>

static const char* TAG = "UI_EVENT_QUEUE";

/* 全局队列句柄 */
static QueueHandle_t s_ui_event_queue = NULL;

/**
 * @brief 初始化UI事件队列
 */
esp_err_t ui_event_queue_init(size_t queue_length) {
    if (s_ui_event_queue != NULL) {
        ESP_LOGW(TAG, "UI event queue already initialized");
        return ESP_OK;
    }

    s_ui_event_queue = xQueueCreate(queue_length, sizeof(lvgl_event_msg_t));
    if (s_ui_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create UI event queue");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "UI event queue initialized with length %d", (int)queue_length);
    return ESP_OK;
}

/**
 * @brief 反初始化UI事件队列
 */
void ui_event_queue_deinit(void) {
    if (s_ui_event_queue != NULL) {
        vQueueDelete(s_ui_event_queue);
        s_ui_event_queue = NULL;
        ESP_LOGI(TAG, "UI event queue deinitialized");
    }
}

/**
 * @brief 向队列发送UI事件（非阻塞）
 */
esp_err_t ui_event_queue_send(const lvgl_event_msg_t* event) {
    if (s_ui_event_queue == NULL || event == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    BaseType_t result = xQueueSendToBack(s_ui_event_queue, (void*)event, 0);
    if (result == pdPASS) {
        return ESP_OK;
    } else if (result == errQUEUE_FULL) {
        ESP_LOGW(TAG, "UI event queue is full, dropping event type: %d", event->type);
        return ESP_ERR_NO_MEM;
    }
    return ESP_FAIL;
}

/**
 * @brief 向队列发送UI事件（可选阻塞）
 */
esp_err_t ui_event_queue_send_with_timeout(const lvgl_event_msg_t* event, uint32_t timeout_ms) {
    if (s_ui_event_queue == NULL || event == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    TickType_t ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
    BaseType_t result = xQueueSendToBack(s_ui_event_queue, (void*)event, ticks);
    
    if (result == pdPASS) {
        return ESP_OK;
    } else if (result == errQUEUE_FULL) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_FAIL;
}

/**
 * @brief 接收UI事件（用于LVGL主任务）
 */
esp_err_t ui_event_queue_receive(lvgl_event_msg_t* event, uint32_t timeout_ms) {
    if (s_ui_event_queue == NULL || event == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    TickType_t ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
    BaseType_t result = xQueueReceive(s_ui_event_queue, (void*)event, ticks);
    
    if (result == pdPASS) {
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

/**
 * @brief 处理UI事件（用于LVGL主任务）
 */
void ui_event_queue_process(const lvgl_event_msg_t* event) {
    if (event == NULL) {
        return;
    }

    switch (event->type) {
        case LVGL_EVENT_TIME_UPDATE:
            /* 获取当前时间标签对象并更新 */
            if (background_manager_is_time_changed()) {
                lv_obj_t* time_label = status_bar_manager_get_time_label();
                if (time_label != NULL) {
                    lv_label_set_text(time_label, event->data.time_data.time_str);
                    lv_obj_invalidate(time_label);
                    background_manager_mark_time_displayed();
                    ESP_LOGD(TAG, "Time updated: %s", event->data.time_data.time_str);
                }
            }
            break;

        case LVGL_EVENT_BATTERY_UPDATE:
            /* 获取当前电池标签对象并更新 */
            if (background_manager_is_battery_changed()) {
                lv_obj_t* battery_label = status_bar_manager_get_battery_label();
                if (battery_label != NULL) {
                    lv_label_set_text(battery_label, event->data.battery_data.battery_str);
                    lv_obj_invalidate(battery_label);
                    background_manager_mark_battery_displayed();
                    ESP_LOGD(TAG, "Battery updated: %s (%d%%)", 
                             event->data.battery_data.battery_str,
                             event->data.battery_data.percentage);
                }
            }
            break;

        case LVGL_EVENT_WIFI_ICON_UPDATE:
            status_bar_manager_set_wifi_status(event->data.wifi_data.connected,
                                               event->data.wifi_data.signal_strength);
            ESP_LOGD(TAG, "WiFi icon updated: connected=%d, signal=%d",
                     event->data.wifi_data.connected,
                     event->data.wifi_data.signal_strength);
            break;

        case LVGL_EVENT_AUDIO_ICON_UPDATE:
            status_bar_manager_show_icon(STATUS_ICON_MUSIC, event->data.audio_data.visible);
            ESP_LOGD(TAG, "Audio icon updated: visible=%d", event->data.audio_data.visible);
            break;

        case LVGL_EVENT_TCP_ICON_UPDATE:
            status_bar_manager_set_tcp_client_status(event->data.tcp_data.connected);
            ESP_LOGD(TAG, "TCP icon updated: connected=%d", event->data.tcp_data.connected);
            break;

        case LVGL_EVENT_CUSTOM:
            ESP_LOGD(TAG, "Custom event processed");
            break;

        default:
            ESP_LOGW(TAG, "Unknown UI event type: %d", event->type);
            break;
    }
}

/**
 * @brief 获取队列中未处理的事件数量
 */
UBaseType_t ui_event_queue_get_count(void) {
    if (s_ui_event_queue == NULL) {
        return 0;
    }
    return uxQueueMessagesWaiting(s_ui_event_queue);
}
