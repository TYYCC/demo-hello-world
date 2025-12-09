/*
 * @Author: tidycraze 2595256284@qq.com
 * @Date: 2025-01-12 20:00:00
 * @LastEditors: tidycraze 2595256284@qq.com
 * @LastEditTime: 2025-01-12 20:00:00
 * @FilePath: \demo-hello-world\main\UI\inc\status_bar_manager.h
 * @Description: 状态栏管理器 - 管理主界面顶部状态栏的更新
 */

#ifndef STATUS_BAR_MANAGER_H
#define STATUS_BAR_MANAGER_H

#include "esp_err.h"
#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 状态栏图标类型
typedef enum {
    STATUS_ICON_WIFI_NONE,          // 无WiFi信号
    STATUS_ICON_WIFI_LOW,           // 低WiFi信号
    STATUS_ICON_WIFI_MEDIUM,        // 中等WiFi信号
    STATUS_ICON_WIFI_HIGH,          // 高WiFi信号
    STATUS_ICON_MUSIC,              // 音频接收模式
    STATUS_ICON_PLUGS_DISCONNECTED, // TCP客户端断开
    STATUS_ICON_PLUGS_CONNECTED,    // TCP客户端连接且有心跳
    STATUS_ICON_MAX
} status_icon_type_t;

// 状态栏图标状态
typedef struct {
    status_icon_type_t type;
    bool visible;
    lv_obj_t* label;
    int x_offset; // 相对于右边电池的偏移量
} status_icon_t;

// 状态栏更新回调函数类型
typedef void (*status_bar_update_cb_t)(void);

/**
 * @brief 初始化状态栏管理器（基本初始化）
 * @return esp_err_t
 */
esp_err_t status_bar_manager_init(void);

/**
 * @brief 设置状态栏容器和回调函数
 * @param status_bar_container 状态栏容器对象
 * @param update_cb 状态更新回调函数
 * @return esp_err_t
 */
esp_err_t status_bar_manager_set_container(lv_obj_t* status_bar_container,
                                           status_bar_update_cb_t update_cb);

/**
 * @brief 设置时间和电池标签对象（保持现有布局）
 * @param time_label 时间标签对象指针
 * @param battery_label 电池标签对象指针
 * @return esp_err_t
 */
esp_err_t status_bar_manager_set_fixed_labels(lv_obj_t* time_label, lv_obj_t* battery_label);

/**
 * @brief 显示指定类型的图标
 * @param icon_type 图标类型
 * @param show 是否显示
 * @return esp_err_t
 */
esp_err_t status_bar_manager_show_icon(status_icon_type_t icon_type, bool show);

/**
 * @brief 根据WiFi信号强度设置WiFi图标
 * @param signal_strength 信号强度 (0-100)，-1表示断开连接
 * @return esp_err_t
 */
esp_err_t status_bar_manager_set_wifi_signal(int signal_strength);

/**
 * @brief 设置音频接收状态
 * @param is_receiving 是否正在接收音频
 * @return esp_err_t
 */
esp_err_t status_bar_manager_set_audio_status(bool is_receiving);

/**
 * @brief 设置TCP客户端连接状态
 * @param has_client_connected 是否有客户端连接且有心跳
 * @return esp_err_t
 */
esp_err_t status_bar_manager_set_tcp_client_status(bool has_client_connected);

/**
 * @brief 启动状态栏更新任务
 * @return esp_err_t
 */
esp_err_t status_bar_manager_start(void);

/**
 * @brief 停止状态栏更新任务
 * @return esp_err_t
 */
esp_err_t status_bar_manager_stop(void);

/**
 * @brief 获取当前显示的图标数量
 * @return int 图标数量
 */
int status_bar_manager_get_visible_icon_count(void);

/**
 * @brief 检查指定图标是否可见
 * @param icon_type 图标类型
 * @return bool 是否可见
 */
bool status_bar_manager_is_icon_visible(status_icon_type_t icon_type);

/**
 * @brief 获取时间标签对象
 * @return lv_obj_t* 时间标签对象指针
 */
lv_obj_t* status_bar_manager_get_time_label(void);

/**
 * @brief 获取电池标签对象
 * @return lv_obj_t* 电池标签对象指针
 */
lv_obj_t* status_bar_manager_get_battery_label(void);

/**
 * @brief 设置WiFi连接状态和信号强度（用于UI事件队列）
 * @param connected 是否连接
 * @param signal_strength 信号强度 (-1: 未连接, 0-3: 信号等级)
 * @return esp_err_t
 */
esp_err_t status_bar_manager_set_wifi_status(bool connected, int signal_strength);

/**
 * @brief 释放状态栏管理器资源
 */
void status_bar_manager_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // STATUS_BAR_MANAGER_H
