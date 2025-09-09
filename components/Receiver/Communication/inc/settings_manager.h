#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// 设置类型枚举
typedef enum {
    SETTING_WIFI_SSID = 0,
    SETTING_WIFI_PASSWORD,
    SETTING_JPEG_QUALITY,
    SETTING_MAX
} setting_type_t;

// 设置值联合体
typedef union {
    char str_value[64];      // 用于字符串设置（WIFI SSID/密码）
    uint8_t uint8_value;     // 用于JPEG质量（0-100）
    uint32_t uint32_value;   // 用于其他数值设置
} setting_value_t;

// 设置项结构
typedef struct {
    setting_type_t type;
    setting_value_t value;
    bool modified;
} setting_item_t;

// 初始化设置管理器
esp_err_t settings_manager_init(void);

// 通过USB接口设置值（支持字符串）
esp_err_t settings_set_via_usb(setting_type_t type, const char* value);

// 通过SPI接口设置值（仅支持HEX格式）
esp_err_t settings_set_via_spi(setting_type_t type, const uint8_t* data, size_t len);

// 获取设置值
esp_err_t settings_get(setting_type_t type, setting_value_t* out_value);

// 保存设置到NVS
esp_err_t settings_save_to_nvs(void);

// 从NVS加载设置
esp_err_t settings_load_from_nvs(void);

// 注册设置变更回调
typedef void (*setting_changed_cb_t)(setting_type_t type, const setting_value_t* new_value);
esp_err_t settings_register_callback(setting_changed_cb_t callback);

#ifdef __cplusplus
}
#endif