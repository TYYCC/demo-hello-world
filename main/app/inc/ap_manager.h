/*
 * AP管理器已被删除，所有AP相关功能已被移除
 * 如果需要恢复AP功能，请取消注释此文件内容
 * ===================================================
 */

// AP管理器已被禁用，所有AP相关功能已被移除
#if 0

#ifndef AP_MANAGER_H
#define AP_MANAGER_H

#include "esp_err.h"
#include "esp_wifi.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// AP状态枚举
typedef enum {
    AP_STATE_DISABLED,
    AP_STATE_STARTING,
    AP_STATE_RUNNING,
    AP_STATE_ERROR
} ap_state_t;

// AP信息结构体
typedef struct {
    ap_state_t state;
    char ssid[33];
    char password[64];
    uint8_t channel;
    uint8_t max_connections;
    char ip_addr[16];
    uint8_t mac_addr[6];
    uint8_t connected_stations;
} ap_info_t;

// AP状态变化回调函数类型
typedef void (*ap_event_cb_t)(ap_state_t state, const char* info);

/**
 * @brief 初始化AP管理器
 * @param event_cb 状态变化回调函数，可为NULL
 * @return esp_err_t
 */
esp_err_t ap_manager_init(ap_event_cb_t event_cb);

/**
 * @brief 启动AP热点
 * @return esp_err_t
 */
esp_err_t ap_manager_start(void);

/**
 * @brief 停止AP热点
 * @return esp_err_t
 */
esp_err_t ap_manager_stop(void);

/**
 * @brief 获取AP信息
 * @return ap_info_t
 */
ap_info_t ap_manager_get_info(void);

/**
 * @brief 设置AP密码
 * @param password 密码字符串（至少8位数字）
 * @return esp_err_t
 */
esp_err_t ap_manager_set_password(const char* password);

/**
 * @brief 获取AP密码
 * @param password 密码缓冲区
 * @param max_len 缓冲区最大长度
 * @return esp_err_t
 */
esp_err_t ap_manager_get_password(char* password, size_t max_len);

/**
 * @brief 检查AP是否正在运行
 * @return true if running, false otherwise
 */
bool ap_manager_is_running(void);

/**
 * @brief 生成默认SSID（基于MAC地址）
 * @param ssid_buf SSID缓冲区
 * @param max_len 缓冲区最大长度
 * @return esp_err_t
 */
esp_err_t ap_manager_generate_default_ssid(char* ssid_buf, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif // AP_MANAGER_H

#endif /* AP manager disabled */
