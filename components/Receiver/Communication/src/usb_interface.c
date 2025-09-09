#include "usb_interface.h"
#include "settings_manager.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "USBInterface";

// USB命令解析
esp_err_t usb_process_command(const char* command) {
    if (command == NULL || strlen(command) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "USB command: %s", command);

    // 解析命令格式: SET <setting> <value>
    char cmd[32], setting[32], value[64];
    int parsed = sscanf(command, "%31s %31s %63[^\n]", cmd, setting, value);
    
    if (parsed < 3 || strcasecmp(cmd, "SET") != 0) {
        ESP_LOGE(TAG, "Invalid command format. Expected: SET <setting> <value>");
        return ESP_ERR_INVALID_ARG;
    }

    // 根据设置名称处理
    if (strcasecmp(setting, "WIFI_SSID") == 0) {
        return settings_set_via_usb(SETTING_WIFI_SSID, value);
    } 
    else if (strcasecmp(setting, "WIFI_PASSWORD") == 0) {
        return settings_set_via_usb(SETTING_WIFI_PASSWORD, value);
    }
    else if (strcasecmp(setting, "JPEG_QUALITY") == 0) {
        return settings_set_via_usb(SETTING_JPEG_QUALITY, value);
    }
    else {
        ESP_LOGE(TAG, "Unknown setting: %s", setting);
        return ESP_ERR_NOT_SUPPORTED;
    }
}

// 获取当前设置状态
esp_err_t usb_get_settings_status(char* buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    setting_value_t wifi_ssid, wifi_password, jpeg_quality;
    
    settings_get(SETTING_WIFI_SSID, &wifi_ssid);
    settings_get(SETTING_WIFI_PASSWORD, &wifi_password);
    settings_get(SETTING_JPEG_QUALITY, &jpeg_quality);

    int written = snprintf(buffer, buffer_size, 
                         "WIFI_SSID: %s\n"
                         "WIFI_PASSWORD: %s\n"
                         "JPEG_QUALITY: %u\n",
                         wifi_ssid.str_value, 
                         wifi_password.str_value,
                         jpeg_quality.uint8_value);

    if (written >= buffer_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

// 保存设置到NVS
esp_err_t usb_save_settings(void) {
    return settings_save_to_nvs();
}

// 恢复默认设置
esp_err_t usb_reset_settings(void) {
    esp_err_t ret;
    
    ret = settings_set_via_usb(SETTING_WIFI_SSID, "");
    if (ret != ESP_OK) return ret;
    
    ret = settings_set_via_usb(SETTING_WIFI_PASSWORD, "");
    if (ret != ESP_OK) return ret;
    
    ret = settings_set_via_usb(SETTING_JPEG_QUALITY, "80");
    if (ret != ESP_OK) return ret;
    
    return settings_save_to_nvs();
}