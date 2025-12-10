/**
 * @file wifi_manager.h
 * @brief WiFi Manager for ESP32 - Simplified WiFi operations
 * @author Gemini
 * @date 2024
 */
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h" // 添加缺失的头文件
#include "esp_wifi_types.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// WiFi状态枚举
typedef enum {
    WIFI_STATE_DISABLED,
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
} wifi_manager_state_t;

// WiFi信息结构体
typedef struct {
    wifi_manager_state_t state;
    char ip_addr[16];
    uint8_t mac_addr[6];
    char ssid[33];       // 当前连接的SSID
} wifi_manager_info_t;

// WiFi扫描结果结构体
typedef struct {
    char ssid[33];
    int16_t rssi;
    wifi_auth_mode_t authmode;
    uint8_t channel;
} wifi_manager_scan_result_t;

// WiFi状态变化时的回调函数类型
typedef void (*wifi_manager_event_cb_t)(void);

/**
 * @brief 初始化WiFi管理器
 * @param event_cb 状态变化时的回调函数, 可为NULL
 * @return esp_err_t
 */
esp_err_t wifi_manager_init(wifi_manager_event_cb_t event_cb);

/**
 * @brief 启动WiFi连接 (STA模式)
 * @note 请在 KConfig 中配置好 SSID 和密码 (CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD)
 * @return esp_err_t
 */
esp_err_t wifi_manager_start(void);

/**
 * @brief 停止WiFi
 * @return esp_err_t
 */
esp_err_t wifi_manager_stop(void);

/**
 * @brief 设置WiFi发射功率
 * @param power_dbm 发射功率, 单位dBm (范围 2 to 20)
 * @return esp_err_t
 */
esp_err_t wifi_manager_set_power(int8_t power_dbm);

/**
 * @brief 获取WiFi发射功率
 * @param power_dbm 指向int8_t的指针，用于存储获取到的功率值 (单位dBm)
 * @return esp_err_t
 */
esp_err_t wifi_manager_get_power(int8_t* power_dbm);

/**
 * @brief 获取当前的WiFi信息
 * @return wifi_manager_info_t 包含当前状态、IP和MAC地址的结构体
 */
wifi_manager_info_t wifi_manager_get_info(void);

/**
 * @brief 注册WiFi事件回调
 * @param event_cb 回调函数，传入NULL可取消注册
 */
void wifi_manager_register_event_callback(wifi_manager_event_cb_t event_cb);

/**
 * @brief 启动时间同步
 */
void wifi_manager_sync_time(void);

/**
 * @brief 获取当前时间字符串
 * @param time_str 输出缓冲区
 * @param max_len 缓冲区最大长度
 * @return 是否成功获取时间
 */
bool wifi_manager_get_time_str(char* time_str, size_t max_len);

/**
 * @brief 获取已保存的WiFi列表大小
 * @return int32_t WiFi数量
 */
int32_t wifi_manager_get_wifi_list_size(void);

/**
 * @brief 根据索引获取WiFi的SSID
 * @param index 索引
 * @return const char* SSID字符串, 如果索引无效则返回NULL
 */
const char* wifi_manager_get_wifi_ssid_by_index(int32_t index);

/**
 * @brief 连接到指定索引的WiFi
 * @param index 要连接的WiFi在列表中的索引
 * @return esp_err_t
 */
esp_err_t wifi_manager_connect_to_index(int32_t index);

/**
 * @brief 获取指定SSID的保存密码
 * @param ssid 网络名称
 * @return const char* 保存的密码，未找到返回NULL
 */
const char* wifi_manager_get_saved_password(const char* ssid);

/**
 * @brief 使用指定SSID和密码进行连接
 * @param ssid 网络名称
 * @param password 网络密码
 * @return esp_err_t
 */
esp_err_t wifi_manager_connect_with_password(const char* ssid, const char* password);

/**
 * @brief 开始扫描WiFi网络
 * @param block 是否阻塞等待扫描完成
 * @return esp_err_t
 */
esp_err_t wifi_manager_start_scan(bool block);

/**
 * @brief 当前是否正在扫描
 */
bool wifi_manager_is_scanning(void);

/**
 * @brief 获取扫描结果数量
 */
size_t wifi_manager_get_scan_result_count(void);

/**
 * @brief 获取扫描结果列表
 * @param results 输出数组
 * @param max_results 数组容量
 * @return 实际写入的数量
 */
size_t wifi_manager_get_scan_results(wifi_manager_scan_result_t* results, size_t max_results);

/**
 * @brief 获取扫描结果版本号，结果更新时会变化
 */
uint32_t wifi_manager_get_scan_results_version(void);

/**
 * @brief 获取最近一次断开连接的原因
 */
wifi_err_reason_t wifi_manager_get_last_disconnect_reason(void);

/**
 * @brief 获取断开事件序列号，用于检测新的断开事件
 */
uint32_t wifi_manager_get_disconnect_sequence(void);

/**
 * @brief 获取WiFi列表版本号，用于缓存管理
 * @return uint32_t 列表版本号，每次列表更新时增加
 */
uint32_t wifi_manager_get_wifi_list_version(void);

/**
 * @brief 从列表中移除指定SSID的WiFi
 * @param ssid 要移除的WiFi的SSID
 * @return esp_err_t ESP_OK表示成功, ESP_ERR_NOT_FOUND表示未找到
 */
esp_err_t wifi_manager_remove_wifi_from_list(const char* ssid);

/**
 * @brief 清空整个WiFi列表
 * @return esp_err_t
 */
esp_err_t wifi_manager_clear_wifi_list(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_MANAGER_H