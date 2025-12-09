/**
 * @file ui_event_queue.h
 * @brief LVGL线程安全的UI事件队列
 * @description 用于在多任务环境中安全地将UI更新事件投递到LVGL主任务
 * @author GitHub Copilot
 * @date 2025-12-09
 */

#ifndef UI_EVENT_QUEUE_H
#define UI_EVENT_QUEUE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lvgl.h"
#include <stdint.h>

/* UI事件类型定义 */
typedef enum {
    LVGL_EVENT_TIME_UPDATE = 0x01,      /* 时间标签更新 */
    LVGL_EVENT_BATTERY_UPDATE = 0x02,   /* 电池标签更新 */
    LVGL_EVENT_WIFI_ICON_UPDATE = 0x03, /* WiFi图标更新 */
    LVGL_EVENT_AUDIO_ICON_UPDATE = 0x04,/* 音频图标更新 */
    LVGL_EVENT_TCP_ICON_UPDATE = 0x05,  /* TCP图标更新 */
    LVGL_EVENT_CUSTOM = 0xFF,           /* 自定义事件 */
} lvgl_event_type_t;

/* UI事件联合体 - 存储不同类型事件的数据 */
typedef struct {
    lvgl_event_type_t type;
    union {
        /* 时间更新事件 */
        struct {
            char time_str[32];
        } time_data;
        
        /* 电池更新事件 */
        struct {
            char battery_str[32];
            uint8_t percentage;
            uint16_t voltage_mv;
            bool is_low_battery;
            bool is_critical;
        } battery_data;
        
        /* WiFi图标更新事件 */
        struct {
            int signal_strength; /* -1: 无连接, 0-3: 信号强度 */
            bool connected;
        } wifi_data;
        
        /* 音频图标更新事件 */
        struct {
            bool visible;
        } audio_data;
        
        /* TCP图标更新事件 */
        struct {
            bool connected;
        } tcp_data;
        
        /* 自定义数据 */
        struct {
            uint32_t data1;
            uint32_t data2;
            void* ptr;
        } custom_data;
    } data;
} lvgl_event_msg_t;

/**
 * @brief 初始化UI事件队列
 * @param queue_length 队列长度，建议值: 16-32
 * @return esp_err_t ESP_OK成功，其他失败
 */
esp_err_t ui_event_queue_init(size_t queue_length);

/**
 * @brief 反初始化UI事件队列
 */
void ui_event_queue_deinit(void);

/**
 * @brief 向队列发送UI事件（非阻塞）
 * @param event 事件指针
 * @return esp_err_t ESP_OK成功，ESP_ERR_INVALID_STATE队列满或未初始化
 */
esp_err_t ui_event_queue_send(const lvgl_event_msg_t* event);

/**
 * @brief 向队列发送UI事件（可选阻塞）
 * @param event 事件指针
 * @param timeout_ms 超时毫秒数，0表示非阻塞
 * @return esp_err_t ESP_OK成功，其他失败
 */
esp_err_t ui_event_queue_send_with_timeout(const lvgl_event_msg_t* event, uint32_t timeout_ms);

/**
 * @brief 接收UI事件（用于LVGL主任务）
 * @param event 事件输出缓冲
 * @param timeout_ms 超时毫秒数，0表示非阻塞
 * @return esp_err_t ESP_OK成功，ESP_ERR_TIMEOUT超时
 */
esp_err_t ui_event_queue_receive(lvgl_event_msg_t* event, uint32_t timeout_ms);

/**
 * @brief 处理UI事件（用于LVGL主任务）
 * @param event 事件
 */
void ui_event_queue_process(const lvgl_event_msg_t* event);

/**
 * @brief 获取队列中未处理的事件数量
 * @return 事件数量
 */
UBaseType_t ui_event_queue_get_count(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* UI_EVENT_QUEUE_H */
