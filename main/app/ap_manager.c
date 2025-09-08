/*
 * AP管理器已被删除，所有AP相关功能已被移除
 * 如果需要恢复AP功能，请取消注释此文件内容
 * ===================================================
 */

// 整个文件已被注释掉
#if 0

#include "ap_manager.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

static const char* TAG = "AP_MANAGER";

// AP配置常量
#define AP_SSID_PREFIX "DisplayTerminal_"
#define AP_DEFAULT_PASSWORD "12345678"
#define AP_CHANNEL 6
#define AP_MAX_CONNECTIONS 4
#define AP_BEACON_INTERVAL 100

// NVS存储键
#define AP_NVS_NAMESPACE "ap_config"
#define AP_NVS_PASSWORD_KEY "password"

// 全局变量
static ap_info_t g_ap_info = {0};
static ap_event_cb_t g_event_cb = NULL;
static esp_netif_t* g_ap_netif = NULL;
static bool g_initialized = false;
static bool g_ap_started = false;

// 事件组
static EventGroupHandle_t s_ap_event_group;
#define AP_STARTED_BIT BIT0
#define AP_STOPPED_BIT BIT1

// 前向声明
static void ap_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id,
                             void* event_data);
static esp_err_t load_password_from_nvs(char* password, size_t max_len);
static esp_err_t save_password_to_nvs(const char* password);

esp_err_t ap_manager_init(ap_event_cb_t event_cb) {
    if (g_initialized) {
        ESP_LOGW(TAG, "AP manager already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    g_event_cb = event_cb;

    // 初始化AP信息结构体
    memset(&g_ap_info, 0, sizeof(g_ap_info));
    g_ap_info.state = AP_STATE_DISABLED;
    g_ap_info.channel = AP_CHANNEL;
    g_ap_info.max_connections = AP_MAX_CONNECTIONS;
    strcpy(g_ap_info.ip_addr, "192.168.4.1");

    // 生成默认SSID
    esp_err_t ret = ap_manager_generate_default_ssid(g_ap_info.ssid, sizeof(g_ap_info.ssid));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to generate default SSID");
        return ret;
    }

    // 从NVS加载密码
    if (load_password_from_nvs(g_ap_info.password, sizeof(g_ap_info.password)) != ESP_OK) {
        // 使用默认密码
        strcpy(g_ap_info.password, AP_DEFAULT_PASSWORD);
        ESP_LOGI(TAG, "Using default password");
    }

    // 创建事件组
    s_ap_event_group = xEventGroupCreate();
    if (!s_ap_event_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_ERR_NO_MEM;
    }

    // 注册WiFi事件处理器
    ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &ap_event_handler, NULL,
                                              NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WiFi event handler");
        vEventGroupDelete(s_ap_event_group);
        return ret;
    }

    g_initialized = true;
    ESP_LOGI(TAG, "AP manager initialized successfully");
    ESP_LOGI(TAG, "Default SSID: %s", g_ap_info.ssid);

    return ESP_OK;
}

esp_err_t ap_manager_start(void) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "AP manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (g_ap_started) {
        ESP_LOGW(TAG, "AP already started");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting AP: %s", g_ap_info.ssid);

    // 创建AP网络接口（如果还没有创建）
    if (!g_ap_netif) {
        g_ap_netif = esp_netif_create_default_wifi_ap();
        if (!g_ap_netif) {
            ESP_LOGE(TAG, "Failed to create AP netif");
            return ESP_FAIL;
        }
    }

    // 配置WiFi
    wifi_config_t wifi_config = {
        .ap =
            {
                .ssid_len = strlen(g_ap_info.ssid),
                .channel = g_ap_info.channel,
                .max_connection = g_ap_info.max_connections,
                .authmode = WIFI_AUTH_WPA2_PSK,
                .beacon_interval = AP_BEACON_INTERVAL,
                .pmf_cfg =
                    {
                        .required = false,
                    },
            },
    };

    strncpy((char*)wifi_config.ap.ssid, g_ap_info.ssid, sizeof(wifi_config.ap.ssid));
    strncpy((char*)wifi_config.ap.password, g_ap_info.password, sizeof(wifi_config.ap.password));

    // 设置WiFi模式为AP
    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi mode to AP: %s", esp_err_to_name(ret));
        return ret;
    }

    // 设置AP配置
    ret = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AP config: %s", esp_err_to_name(ret));
        return ret;
    }

    // 启动WiFi
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(ret));
        return ret;
    }

    g_ap_info.state = AP_STATE_STARTING;
    g_ap_started = true;

    if (g_event_cb) {
        g_event_cb(AP_STATE_STARTING, "Starting AP...");
    }

    ESP_LOGI(TAG, "AP start command sent");
    return ESP_OK;
}

esp_err_t ap_manager_stop(void) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "AP manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!g_ap_started) {
        ESP_LOGW(TAG, "AP not started");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping AP");

    esp_err_t ret = esp_wifi_stop();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop WiFi: %s", esp_err_to_name(ret));
        return ret;
    }

    g_ap_info.state = AP_STATE_DISABLED;
    g_ap_info.connected_stations = 0;
    g_ap_started = false;

    if (g_event_cb) {
        g_event_cb(AP_STATE_DISABLED, "AP stopped");
    }

    // 通知状态栏管理器AP已停止
    extern esp_err_t status_bar_manager_set_ap_status(bool is_running);
    status_bar_manager_set_ap_status(false);
    ESP_LOGI(TAG, "AP状态已更新为停止");

    ESP_LOGI(TAG, "AP stopped");
    return ESP_OK;
}

ap_info_t ap_manager_get_info(void) { return g_ap_info; }

esp_err_t ap_manager_set_password(const char* password) {
    if (!password) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t len = strlen(password);
    if (len < 8 || len >= sizeof(g_ap_info.password)) {
        ESP_LOGE(TAG, "Password length must be 8-63 characters");
        return ESP_ERR_INVALID_ARG;
    }

    // 检查密码是否只包含数字
    for (size_t i = 0; i < len; i++) {
        if (password[i] < '0' || password[i] > '9') {
            ESP_LOGE(TAG, "Password must contain only digits");
            return ESP_ERR_INVALID_ARG;
        }
    }

    strcpy(g_ap_info.password, password);

    // 保存到NVS
    esp_err_t ret = save_password_to_nvs(password);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save password to NVS: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "AP password updated");
    return ESP_OK;
}

esp_err_t ap_manager_get_password(char* password, size_t max_len) {
    if (!password || max_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(password, g_ap_info.password, max_len - 1);
    password[max_len - 1] = '\0';
    return ESP_OK;
}

bool ap_manager_is_running(void) { return g_ap_started && (g_ap_info.state == AP_STATE_RUNNING); }

esp_err_t ap_manager_generate_default_ssid(char* ssid_buf, size_t max_len) {
    if (!ssid_buf || max_len < 32) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t mac[6];
    esp_err_t ret = esp_wifi_get_mac(WIFI_IF_AP, mac);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get MAC address: %s", esp_err_to_name(ret));
        return ret;
    }

    snprintf(ssid_buf, max_len, "%s%02X%02X", AP_SSID_PREFIX, mac[4], mac[5]);
    return ESP_OK;
}

// 私有函数实现
static void ap_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id,
                             void* event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG, "AP started successfully (ap_manager)");
            g_ap_info.state = AP_STATE_RUNNING;
            xEventGroupSetBits(s_ap_event_group, AP_STARTED_BIT);
            if (g_event_cb) {
                g_event_cb(AP_STATE_RUNNING, "AP running");
            }

            // 通知状态栏管理器AP已启动
            extern esp_err_t status_bar_manager_set_ap_status(bool is_running);
            status_bar_manager_set_ap_status(true);
            ESP_LOGI(TAG, "AP状态已更新为运行中 (ap_manager)");
            break;

        case WIFI_EVENT_AP_STOP:
            ESP_LOGI(TAG, "AP stopped");
            g_ap_info.state = AP_STATE_DISABLED;
            g_ap_info.connected_stations = 0;
            xEventGroupSetBits(s_ap_event_group, AP_STOPPED_BIT);
            if (g_event_cb) {
                g_event_cb(AP_STATE_DISABLED, "AP stopped");
            }

            // 通知状态栏管理器AP已停止
            extern esp_err_t status_bar_manager_set_ap_status(bool is_running);
            status_bar_manager_set_ap_status(false);
            ESP_LOGI(TAG, "AP状态已更新为停止 (ap_manager)");
            break;

        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*)event_data;
            ESP_LOGI(TAG, "Station %02x:%02x:%02x:%02x:%02x:%02x connected, AID=%d", event->mac[0],
                     event->mac[1], event->mac[2], event->mac[3], event->mac[4], event->mac[5],
                     event->aid);
            g_ap_info.connected_stations++;
            if (g_event_cb) {
                g_event_cb(AP_STATE_RUNNING, "Station connected");
            }
            break;
        }

        case WIFI_EVENT_AP_STADISCONNECTED: {
            wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*)event_data;
            ESP_LOGI(TAG, "Station %02x:%02x:%02x:%02x:%02x:%02x disconnected, AID=%d",
                     event->mac[0], event->mac[1], event->mac[2], event->mac[3], event->mac[4],
                     event->mac[5], event->aid);
            if (g_ap_info.connected_stations > 0) {
                g_ap_info.connected_stations--;
            }
            if (g_event_cb) {
                g_event_cb(AP_STATE_RUNNING, "Station disconnected");
            }
            break;
        }

        default:
            break;
        }
    }
}

static esp_err_t load_password_from_nvs(char* password, size_t max_len) {
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(AP_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }

    size_t required_size = max_len;
    ret = nvs_get_str(nvs_handle, AP_NVS_PASSWORD_KEY, password, &required_size);
    nvs_close(nvs_handle);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Password loaded from NVS");
    }

    return ret;
}

static esp_err_t save_password_to_nvs(const char* password) {
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(AP_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_str(nvs_handle, AP_NVS_PASSWORD_KEY, password);
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Password saved to NVS");
    }

    return ret;
}

#endif /* AP manager disabled */
