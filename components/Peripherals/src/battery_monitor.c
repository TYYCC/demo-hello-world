#include "battery_monitor.h"
#include "driver/adc.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <math.h>

static const char* TAG = "BATTERY_MONITOR";
static bool s_is_initialized = false;
static float s_filtered_voltage = 0.0f;

// 校准相关变量
static battery_cal_data_t s_cal_data = {.voltage_offset = 0.0f,
                                        .voltage_scale = 1.0f,
                                        .min_voltage_mv = BATTERY_VOLTAGE_MIN,
                                        .max_voltage_mv = BATTERY_VOLTAGE_MAX,
                                        .is_calibrated = false};
static bool s_is_calibrating = false;
static int s_calibration_known_voltage = 0;

static int adc_raw_to_voltage_mv(int adc_reading) {
    // 使用简单的线性转换，将ADC原始值转换为电压
    // ADC1在11dB衰减下，0-3.3V对应0-4095
    int voltage = (adc_reading * 3300) / 4095; // 转换为mV

    // 应用分压比例
    voltage = (int)(voltage * BATTERY_VOLTAGE_DIVIDER_RATIO);

    // 应用校准数据
    if (s_cal_data.is_calibrated) {
        voltage = (int)(voltage * s_cal_data.voltage_scale + s_cal_data.voltage_offset);
    }

    return voltage;
}

static int calculate_percentage(int voltage_mv) {
    int min_voltage = s_cal_data.is_calibrated ? s_cal_data.min_voltage_mv : BATTERY_VOLTAGE_MIN;
    int max_voltage = s_cal_data.is_calibrated ? s_cal_data.max_voltage_mv : BATTERY_VOLTAGE_MAX;

    if (voltage_mv <= min_voltage) {
        return 0;
    } else if (voltage_mv >= max_voltage) {
        return 100;
    } else {
        int range = max_voltage - min_voltage;
        int voltage_range = voltage_mv - min_voltage;
        return (voltage_range * 100) / range;
    }
}

static battery_status_t determine_battery_status(int voltage_mv, int percentage) {
    if (percentage <= 5) {
        return BATTERY_STATUS_EMPTY;
    } else if (percentage >= 95) {
        return BATTERY_STATUS_FULL;
    } else {
        return BATTERY_STATUS_DISCHARGING;
    }
}

esp_err_t battery_monitor_init(void) {
    if (s_is_initialized) {
        return ESP_OK;
    }

    esp_err_t ret;

    // 配置ADC1
    ret = adc1_config_width(ADC_WIDTH_BIT_DEFAULT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to config ADC width: %s", esp_err_to_name(ret));
        return ret;
    }

    // 配置ADC通道
    ret = adc1_config_channel_atten(BATTERY_ADC_CHANNEL, BATTERY_ADC_ATTEN);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to config ADC channel: %s", esp_err_to_name(ret));
        return ret;
    }

    // 读取初始ADC值
    int adc_reading = adc1_get_raw(BATTERY_ADC_CHANNEL);
    s_filtered_voltage = (float)adc_raw_to_voltage_mv(adc_reading);

    // 尝试从NVS加载校准数据
    if (battery_monitor_load_calibration_from_nvs() == ESP_OK) {
        ESP_LOGD(TAG, "Successfully loaded calibration data from NVS");
    } else {
        ESP_LOGD(TAG, "No calibration data found in NVS, using default values");
    }

    s_is_initialized = true;
    ESP_LOGD(TAG, "Battery monitor initialized successfully");
    return ESP_OK;
}

esp_err_t battery_monitor_deinit(void) {
    s_is_initialized = false;
    ESP_LOGD(TAG, "Battery monitor de-initialized");
    return ESP_OK;
}

esp_err_t battery_monitor_read(battery_info_t* info) {
    if (!s_is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int adc_reading = adc1_get_raw(BATTERY_ADC_CHANNEL);
    int voltage_mv = adc_raw_to_voltage_mv(adc_reading);

    s_filtered_voltage = BATTERY_FILTER_ALPHA * voltage_mv + (1.0f - BATTERY_FILTER_ALPHA) * s_filtered_voltage;
    voltage_mv = (int)s_filtered_voltage;

    int percentage = calculate_percentage(voltage_mv);
    battery_status_t status = determine_battery_status(voltage_mv, percentage);
    bool is_low_battery = (percentage <= BATTERY_PERCENT_LOW);
    bool is_critical = (percentage <= BATTERY_PERCENT_CRITICAL);

    info->voltage_mv = voltage_mv;
    info->percentage = percentage;
    info->status = status;
    info->is_low_battery = is_low_battery;
    info->is_critical = is_critical;

    ESP_LOGD(TAG, "Battery: %dmV, %d%%, status: %d", voltage_mv, percentage, status);

    return ESP_OK;
}

esp_err_t battery_monitor_get_voltage(int* voltage_mv) {
    if (!s_is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (voltage_mv == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    battery_info_t info;
    esp_err_t ret = battery_monitor_read(&info);
    if (ret == ESP_OK) {
        *voltage_mv = info.voltage_mv;
    }
    return ret;
}

esp_err_t battery_monitor_get_percentage(int* percentage) {
    if (!s_is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (percentage == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    battery_info_t info;
    esp_err_t ret = battery_monitor_read(&info);
    if (ret == ESP_OK) {
        *percentage = info.percentage;
    }
    return ret;
}

bool battery_monitor_is_low_battery(void) {
    if (!s_is_initialized) {
        return false;
    }

    battery_info_t info;
    if (battery_monitor_read(&info) == ESP_OK) {
        return info.is_low_battery;
    }
    return false;
}

bool battery_monitor_is_critical_battery(void) {
    if (!s_is_initialized) {
        return false;
    }

    battery_info_t info;
    if (battery_monitor_read(&info) == ESP_OK) {
        return info.is_critical;
    }
    return false;
}

// ========================================
// 校准功能实现
// ========================================

esp_err_t battery_monitor_start_calibration(int known_voltage_mv) {
    if (!s_is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    s_is_calibrating = true;
    s_calibration_known_voltage = known_voltage_mv;
    ESP_LOGI(TAG, "Started calibration with known voltage: %dmV", known_voltage_mv);
    return ESP_OK;
}

esp_err_t battery_monitor_finish_calibration(void) {
    if (!s_is_initialized || !s_is_calibrating) {
        return ESP_ERR_INVALID_STATE;
    }

    // 读取当前ADC值
    int adc_reading = adc1_get_raw(BATTERY_ADC_CHANNEL);

    // 计算校准参数
    int raw_voltage = adc_raw_to_voltage_mv(adc_reading);

    // 计算校准系数
    float expected_voltage = (float)s_calibration_known_voltage;
    float measured_voltage = (float)raw_voltage;

    if (measured_voltage > 0) {
        s_cal_data.voltage_scale = expected_voltage / measured_voltage;
        s_cal_data.voltage_offset = 0.0f; // 可以扩展为偏移量校准
        s_cal_data.is_calibrated = true;

        ESP_LOGI(TAG, "Calibration completed: scale=%.3f, measured=%dmV, expected=%dmV", s_cal_data.voltage_scale,
                 raw_voltage, s_calibration_known_voltage);
    } else {
        ESP_LOGE(TAG, "Invalid measured voltage for calibration: %dmV", raw_voltage);
        s_is_calibrating = false;
        return ESP_ERR_INVALID_ARG;
    }

    s_is_calibrating = false;
    return ESP_OK;
}

esp_err_t battery_monitor_cancel_calibration(void) {
    s_is_calibrating = false;
    ESP_LOGI(TAG, "Calibration cancelled");
    return ESP_OK;
}

bool battery_monitor_is_calibrating(void) { return s_is_calibrating; }

bool battery_monitor_is_calibrated(void) { return s_cal_data.is_calibrated; }

esp_err_t battery_monitor_load_calibration_from_nvs(void) {
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(BATTERY_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }

    size_t required_size = sizeof(battery_cal_data_t);
    ret = nvs_get_blob(nvs_handle, BATTERY_NVS_CAL_KEY, &s_cal_data, &required_size);
    nvs_close(nvs_handle);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Loaded calibration data: scale=%.3f, offset=%.1f", s_cal_data.voltage_scale,
                 s_cal_data.voltage_offset);
    }

    return ret;
}

esp_err_t battery_monitor_save_calibration_to_nvs(void) {
    if (!s_cal_data.is_calibrated) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(BATTERY_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_blob(nvs_handle, BATTERY_NVS_CAL_KEY, &s_cal_data, sizeof(battery_cal_data_t));
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration data saved to NVS");
    }

    return ret;
}

esp_err_t battery_monitor_reset_calibration(void) {
    s_cal_data.voltage_offset = 0.0f;
    s_cal_data.voltage_scale = 1.0f;
    s_cal_data.min_voltage_mv = BATTERY_VOLTAGE_MIN;
    s_cal_data.max_voltage_mv = BATTERY_VOLTAGE_MAX;
    s_cal_data.is_calibrated = false;

    // 从NVS中删除校准数据
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(BATTERY_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret == ESP_OK) {
        nvs_erase_key(nvs_handle, BATTERY_NVS_CAL_KEY);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }

    ESP_LOGI(TAG, "Calibration data reset to default values");
    return ESP_OK;
}