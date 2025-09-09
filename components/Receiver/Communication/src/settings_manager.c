/*
 * @Author: tidycraze 2595256284@qq.com
 * @Date: 2025-09-03 15:47:45
 * @LastEditors: tidycraze 2595256284@qq.com
 * @LastEditTime: 2025-09-03 15:58:57
 * @FilePath: \demo-hello-world\components\Receiver\src\settings_manager.c
 * @Description: 
 * 
 */
#include "settings_manager.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "SettingsManager";

// 默认设置值
static setting_item_t s_settings[SETTING_MAX] = {
    [SETTING_WIFI_SSID] = {.type = SETTING_WIFI_SSID, .value.str_value = "", .modified = false},
    [SETTING_WIFI_PASSWORD] = {.type = SETTING_WIFI_PASSWORD, .value.str_value = "", .modified = false},
    [SETTING_JPEG_QUALITY] = {.type = SETTING_JPEG_QUALITY, .value.uint8_value = 80, .modified = false}
};

static setting_changed_cb_t s_callback = NULL;
static nvs_handle_t s_nvs_handle = 0;

// 设置项名称映射
static const char* setting_names[SETTING_MAX] = {
    [SETTING_WIFI_SSID] = "wifi_ssid",
    [SETTING_WIFI_PASSWORD] = "wifi_password", 
    [SETTING_JPEG_QUALITY] = "jpeg_quality"
};

esp_err_t settings_manager_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ret = nvs_open("settings", NVS_READWRITE, &s_nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    // 从NVS加载设置
    settings_load_from_nvs();
    
    ESP_LOGI(TAG, "Settings manager initialized");
    return ESP_OK;
}

esp_err_t settings_set_via_usb(setting_type_t type, const char* value) {
    if (type >= SETTING_MAX || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "USB set %d: %s", type, value);

    switch (type) {
        case SETTING_WIFI_SSID:
        case SETTING_WIFI_PASSWORD:
            strncpy(s_settings[type].value.str_value, value, sizeof(s_settings[type].value.str_value) - 1);
            s_settings[type].value.str_value[sizeof(s_settings[type].value.str_value) - 1] = '\0';
            break;
            
        case SETTING_JPEG_QUALITY:
            s_settings[type].value.uint8_value = (uint8_t)atoi(value);
            break;
            
        default:
            return ESP_ERR_NOT_SUPPORTED;
    }

    s_settings[type].modified = true;
    
    // 触发回调
    if (s_callback) {
        s_callback(type, &s_settings[type].value);
    }
    
    return ESP_OK;
}

esp_err_t settings_set_via_spi(setting_type_t type, const uint8_t* data, size_t len) {
    if (type >= SETTING_MAX || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "SPI set %d: len=%d", type, len);

    switch (type) {
        case SETTING_JPEG_QUALITY:
            if (len >= 1) {
                s_settings[type].value.uint8_value = data[0];
                s_settings[type].modified = true;
                
                if (s_callback) {
                    s_callback(type, &s_settings[type].value);
                }
                return ESP_OK;
            }
            break;
            
        default:
            ESP_LOGW(TAG, "SPI setting type %d not supported", type);
            return ESP_ERR_NOT_SUPPORTED;
    }
    
    return ESP_ERR_INVALID_SIZE;
}

esp_err_t settings_get(setting_type_t type, setting_value_t* out_value) {
    if (type >= SETTING_MAX || out_value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *out_value = s_settings[type].value;
    return ESP_OK;
}

esp_err_t settings_save_to_nvs(void) {
    esp_err_t ret = ESP_OK;
    
    for (int i = 0; i < SETTING_MAX; i++) {
        if (s_settings[i].modified) {
            switch (i) {
                case SETTING_WIFI_SSID:
                case SETTING_WIFI_PASSWORD:
                    ret = nvs_set_str(s_nvs_handle, setting_names[i], s_settings[i].value.str_value);
                    break;
                    
                case SETTING_JPEG_QUALITY:
                    ret = nvs_set_u8(s_nvs_handle, setting_names[i], s_settings[i].value.uint8_value);
                    break;
                    
                default:
                    continue;
            }
            
            if (ret == ESP_OK) {
                s_settings[i].modified = false;
            } else {
                ESP_LOGE(TAG, "Failed to save %s: %s", setting_names[i], esp_err_to_name(ret));
            }
        }
    }
    
    ret = nvs_commit(s_nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

esp_err_t settings_load_from_nvs(void) {
    esp_err_t ret = ESP_OK;
    size_t required_size = 0;
    
    for (int i = 0; i < SETTING_MAX; i++) {
        switch (i) {
            case SETTING_WIFI_SSID:
            case SETTING_WIFI_PASSWORD:
                ret = nvs_get_str(s_nvs_handle, setting_names[i], NULL, &required_size);
                if (ret == ESP_OK && required_size > 0) {
                    ret = nvs_get_str(s_nvs_handle, setting_names[i], s_settings[i].value.str_value, &required_size);
                }
                break;
                
            case SETTING_JPEG_QUALITY:
                ret = nvs_get_u8(s_nvs_handle, setting_names[i], &s_settings[i].value.uint8_value);
                break;
                
            default:
                continue;
        }
        
        if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to load %s: %s", setting_names[i], esp_err_to_name(ret));
        }
        
        s_settings[i].modified = false;
    }
    
    return ESP_OK;
}

esp_err_t settings_register_callback(setting_changed_cb_t callback) {
    s_callback = callback;
    return ESP_OK;
}