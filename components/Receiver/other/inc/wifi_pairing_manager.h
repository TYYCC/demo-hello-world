#ifndef WIFI_PAIRING_MANAGER_H
#define WIFI_PAIRING_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

// WiFi配对状态枚举
typedef enum {
    WIFI_PAIRING_STATE_IDLE = 0,        // 空闲状态
    WIFI_PAIRING_STATE_SCANNING,        // 扫描中
    WIFI_PAIRING_STATE_CONNECTING,      // 连接中
    WIFI_PAIRING_STATE_CONNECTED,       // 已连接
    WIFI_PAIRING_STATE_DISCONNECTED,    // 连接断开
    WIFI_PAIRING_STATE_ERROR            // 错误状态
} wifi_pairing_state_t;

// WiFi凭证结构体
typedef struct {
    char ssid[33];      // SSID，最大32字符+结束符
    char password[65];  // 密码，最大64字符+结束符
    bool valid;         // 凭证是否有效
} wifi_credentials_t;

// WiFi配对管理器配置
typedef struct {
    uint32_t scan_interval_ms;      // 扫描间隔（毫秒）
    uint8_t task_priority;          // 任务优先级
    uint32_t task_stack_size;       // 任务栈大小
    uint32_t connection_timeout_ms; // 连接超时时间（毫秒）
    char target_ssid_prefix[16];    // 目标SSID前缀
    char default_password[65];      // 默认密码
} wifi_pairing_config_t;

// 默认配置
#define WIFI_PAIRING_DEFAULT_CONFIG() \
    { \
        .scan_interval_ms = 5000, \
        .task_priority = 3, \
        .task_stack_size = 4096, \
        .connection_timeout_ms = 10000, \
        .target_ssid_prefix = "ESP32_Terminal_", \
        .default_password = "12345678" \
    }

// WiFi配对事件回调函数类型
typedef void (*wifi_pairing_event_cb_t)(wifi_pairing_state_t state, const char* ssid);

/**
 * @brief 初始化WiFi配对管理器
 * @param config 配置参数
 * @param event_cb 事件回调函数（可选）
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t wifi_pairing_manager_init(const wifi_pairing_config_t* config, wifi_pairing_event_cb_t event_cb);

/**
 * @brief 反初始化WiFi配对管理器
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t wifi_pairing_manager_deinit(void);

/**
 * @brief 启动WiFi配对管理器
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t wifi_pairing_manager_start(void);

/**
 * @brief 停止WiFi配对管理器
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t wifi_pairing_manager_stop(void);

/**
 * @brief 获取当前WiFi配对状态
 * @return 当前状态
 */
wifi_pairing_state_t wifi_pairing_get_state(void);

/**
 * @brief 获取当前连接的WiFi信息
 * @param credentials 输出参数，存储WiFi凭证
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t wifi_pairing_get_current_credentials(wifi_credentials_t* credentials);

/**
 * @brief 清除存储的WiFi凭证
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t wifi_pairing_clear_credentials(void);

/**
 * @brief 手动触发WiFi扫描
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t wifi_pairing_trigger_scan(void);

/**
 * @brief 检查WiFi配对管理器是否已初始化
 * @return true 已初始化，false 未初始化
 */
bool wifi_pairing_manager_is_initialized(void);

/**
 * @brief 检查WiFi配对管理器是否正在运行
 * @return true 正在运行，false 未运行
 */
bool wifi_pairing_manager_is_running(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_PAIRING_MANAGER_H