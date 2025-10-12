#include "wifi_manager.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_wifi_types_generic.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

static const char* TAG = "WIFI_MANAGER";

// FreeRTOS event group to signal when we are connected
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static int s_retry_num = 0;

// 全局变量来保存WiFi信息和回调
static wifi_manager_info_t g_wifi_info;
static wifi_manager_event_cb_t g_event_cb = NULL;

#define WIFI_MANAGER_MAX_SCAN_RESULTS 32
static wifi_ap_record_t s_scan_records[WIFI_MANAGER_MAX_SCAN_RESULTS];
static uint16_t s_scan_count = 0;
static bool s_scan_in_progress = false;
static uint32_t s_scan_results_version = 0;
static wifi_err_reason_t s_last_disconnect_reason = WIFI_REASON_UNSPECIFIED;
static uint32_t s_disconnect_sequence = 0;

// 用于定期检查WiFi带宽的定时器
static TimerHandle_t wifi_bandwidth_check_timer = NULL;
static const int WIFI_BW_CHECK_INTERVAL_MS = 30000; // 30秒检查一次带宽

// 函数前向声明
static void start_wifi_bandwidth_check_timer(void);
static void check_wifi_bandwidth_timer_cb(TimerHandle_t xTimer);
static void add_wifi_to_list(const char* ssid, const char* password);
static const char* find_wifi_password(const char* ssid);

#define WIFI_NVS_NAMESPACE "wifi_config"
#define WIFI_NVS_KEY_SSID "ssid"
#define WIFI_NVS_KEY_PASSWORD "password"

#define MAX_WIFI_LIST_SIZE 256

typedef struct {
    char ssid[32];
    char password[64];
} wifi_config_entry_t;

static wifi_config_entry_t wifi_list[MAX_WIFI_LIST_SIZE] = {
    {"tidy", "22989822"},
    {"Sysware-AP", "syswareonline.com"},
    {"Xiaomi13", "22989822"},
    {"TiydC", "22989822"},
};
static int32_t wifi_list_size = 4;

// 前向声明
static bool load_wifi_config_from_nvs(char* ssid, size_t ssid_len, char* password,
                                      size_t password_len);
static void save_wifi_config_to_nvs(const char* ssid, const char* password);
static void save_wifi_list_to_nvs(void);
static void load_wifi_list_from_nvs(void);

/**
 * @brief WiFi事件处理器
 */
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id,
                          void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        g_wifi_info.state = WIFI_STATE_CONNECTING;
        esp_wifi_connect();
        ESP_LOGI(TAG, "STA Start, connecting to AP...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* disconnected = (wifi_event_sta_disconnected_t*)event_data;
        g_wifi_info.state = WIFI_STATE_DISCONNECTED;
        strcpy(g_wifi_info.ip_addr, "N/A");
        memset(g_wifi_info.ssid, 0, sizeof(g_wifi_info.ssid)); // 清空SSID

        // 重置重试计数，避免自动重连
        s_retry_num = 0;
        if (disconnected) {
            s_last_disconnect_reason = disconnected->reason;
        } else {
            s_last_disconnect_reason = WIFI_REASON_UNSPECIFIED;
        }
        s_disconnect_sequence++;
        ESP_LOGW(TAG, "WiFi disconnected, reason: %d", s_last_disconnect_reason);

        // 如果设置了回调函数，则调用它
        if (g_event_cb) {
            g_event_cb();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        s_scan_in_progress = false;
        wifi_event_sta_scan_done_t* scan_done = (wifi_event_sta_scan_done_t*)event_data;
        if (scan_done && scan_done->status != 0) {
            ESP_LOGW(TAG, "WiFi scan finished with status: %d", scan_done->status);
            s_scan_count = 0;
            s_scan_results_version++;
        } else {
            uint16_t number = WIFI_MANAGER_MAX_SCAN_RESULTS;
            esp_err_t err = esp_wifi_scan_get_ap_records(&number, s_scan_records);
            if (err == ESP_OK) {
                s_scan_count = number;
                s_scan_results_version++;
                ESP_LOGI(TAG, "WiFi scan completed: %u APs", number);
            } else {
                ESP_LOGW(TAG, "Failed to get scan records: %s", esp_err_to_name(err));
                s_scan_count = 0;
                s_scan_results_version++;
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        snprintf(g_wifi_info.ip_addr, sizeof(g_wifi_info.ip_addr), IPSTR,
                 IP2STR(&event->ip_info.ip));
        g_wifi_info.state = WIFI_STATE_CONNECTED;
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "Got IP address: %s", g_wifi_info.ip_addr);

        // 获取并保存当前连接的SSID
        wifi_config_t wifi_config;
        esp_wifi_get_config(WIFI_IF_STA, &wifi_config);
        strncpy(g_wifi_info.ssid, (char*)wifi_config.sta.ssid, sizeof(g_wifi_info.ssid) - 1);

        // Add connected WiFi to the list and persist credentials
        add_wifi_to_list((char*)wifi_config.sta.ssid, (char*)wifi_config.sta.password);
        save_wifi_config_to_nvs((char*)wifi_config.sta.ssid, (char*)wifi_config.sta.password);

        ESP_LOGI(TAG, "Starting time synchronization...");
        wifi_manager_sync_time();
    }

    // 如果设置了回调函数，则调用它
    if (g_event_cb) {
        g_event_cb();
    }
}

/**
 * @brief 初始化WiFi底层 (NVS, Netif, Event Loop)
 */
static esp_err_t wifi_init_stack(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());

    // 检查事件循环是否已经存在，避免重复创建
    ret = esp_event_loop_create_default();
    if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Event loop already exists, skipping creation");
    } else {
        ESP_ERROR_CHECK(ret);
    }

    esp_netif_create_default_wifi_sta();

    // 初始化WiFi驱动
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    // 注册事件处理器
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &event_handler, NULL, &instance_got_ip));
    return ESP_OK;
}

esp_err_t wifi_manager_init(wifi_manager_event_cb_t event_cb) {
    g_event_cb = event_cb;
    memset(&g_wifi_info, 0, sizeof(g_wifi_info));
    g_wifi_info.state = WIFI_STATE_DISABLED;
    strcpy(g_wifi_info.ip_addr, "N/A");
    memset(g_wifi_info.ssid, 0, sizeof(g_wifi_info.ssid));

    // 先初始化WiFi底层
    esp_err_t ret = wifi_init_stack();
    if (ret != ESP_OK) {
        return ret;
    }

    // 获取MAC地址
    ret = esp_wifi_get_mac(ESP_IF_WIFI_STA, g_wifi_info.mac_addr);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get MAC address: %s", esp_err_to_name(ret));
    }

    return ESP_OK;
}

esp_err_t wifi_manager_start(void) {
    s_wifi_event_group = xEventGroupCreate();

    load_wifi_list_from_nvs();

    wifi_config_t wifi_config = {0};

    // 优先使用上次成功连接的WiFi
    if (load_wifi_config_from_nvs((char*)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid),
                                  (char*)wifi_config.sta.password,
                                  sizeof(wifi_config.sta.password))) {
        ESP_LOGI(TAG, "Attempting to connect to last known WiFi: %s", wifi_config.sta.ssid);
    } else if (wifi_list_size > 0) {
        // 否则，尝试列表中的第一个WiFi
        strncpy((char*)wifi_config.sta.ssid, wifi_list[0].ssid, sizeof(wifi_config.sta.ssid));
        strncpy((char*)wifi_config.sta.password, wifi_list[0].password,
                sizeof(wifi_config.sta.password));
        ESP_LOGI(TAG, "Attempting to connect to WiFi from list: %s", wifi_config.sta.ssid);
    } else {
        // 如果都没有，则使用默认配置
        ESP_LOGW(TAG, "No saved WiFi configuration found, using default.");
        strncpy((char*)wifi_config.sta.ssid, "TidyC", sizeof(wifi_config.sta.ssid));
        strncpy((char*)wifi_config.sta.password, "22989822", sizeof(wifi_config.sta.password));
    }

    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // 尝试设置 STA 带宽为 HT40，如失败则回退 HT20（只在启动时尝试一次，避免频繁切带宽影响稳定性）
    esp_err_t bandwidth_ret = esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT40);
    if (bandwidth_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set WiFi bandwidth to HT40, trying HT20: %s", esp_err_to_name(bandwidth_ret));
        bandwidth_ret = esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
        if (bandwidth_ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to set WiFi bandwidth to HT20: %s", esp_err_to_name(bandwidth_ret));
        }
    }

    // WiFi 启动后设置发射功率（使用 dBm 语义，避免 quarter-dBm 误用导致功率过低）
    esp_err_t power_ret = wifi_manager_set_power(20); // 20 dBm
    if (power_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set WiFi power: %s", esp_err_to_name(power_ret));
    }
    
    // 启动WiFi带宽检查定时器（为提升连接稳定性，先禁用定时强制切换带宽）
    // start_wifi_bandwidth_check_timer();

    ESP_LOGI(TAG, "wifi_manager_start finished, connection is in progress...");
    return ESP_OK;
}

esp_err_t wifi_manager_stop(void) {
    esp_err_t err = esp_wifi_stop();
    if (err == ESP_OK) {
        g_wifi_info.state = WIFI_STATE_DISABLED;
        strcpy(g_wifi_info.ip_addr, "N/A");
        memset(g_wifi_info.ssid, 0, sizeof(g_wifi_info.ssid)); // 清空SSID
        if (g_event_cb)
            g_event_cb();
    }
    if (s_wifi_event_group) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }
    if (s_scan_count > 0) {
        s_scan_count = 0;
        s_scan_results_version++;
    }
    s_scan_in_progress = false;
    ESP_LOGI(TAG, "WiFi stopped.");
    return err;
}

esp_err_t wifi_manager_set_power(int8_t power_dbm) {
    if (power_dbm < 2 || power_dbm > 20) {
        return ESP_ERR_INVALID_ARG;
    }
    // ESP-IDF 内部使用 0.25dBm 为单位, 8 -> 2dBm, 80 -> 20dBm
    int8_t power_val = (int8_t)(power_dbm * 4);
    esp_err_t err = esp_wifi_set_max_tx_power(power_val);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WiFi Tx Power set to %d dBm", power_dbm);
    }
    return err;
}

esp_err_t wifi_manager_get_power(int8_t* power_dbm) {
    if (power_dbm == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    int8_t power_val = 0;
    esp_err_t err = esp_wifi_get_max_tx_power(&power_val);
    if (err == ESP_OK) {
        *power_dbm = power_val / 4; // Convert from 0.25dBm units to dBm
        ESP_LOGI(TAG, "WiFi Tx Power get: %d dBm", *power_dbm);
    }
    return err;
}

wifi_manager_info_t wifi_manager_get_info(void) { return g_wifi_info; }

void wifi_manager_register_event_callback(wifi_manager_event_cb_t event_cb) {
    g_event_cb = event_cb;
}

/**
 * @brief 时间同步回调函数
 */
static void time_sync_notification_cb(struct timeval* tv) { ESP_LOGI(TAG, "Time synchronized!"); }

/**
 * @brief 启动时间同步
 */
void wifi_manager_sync_time(void) {
    ESP_LOGI(TAG, "Initializing SNTP time sync...");

    // 设置时区为北京时间 (UTC+8)
    setenv("TZ", "CST-8", 1);
    tzset();

    // 配置SNTP
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com"); // 阿里云NTP服务器
    esp_sntp_setservername(1, "ntp1.aliyun.com");
    esp_sntp_setservername(2, "ntp2.aliyun.com");

    // 设置更新间隔（1小时）
    esp_sntp_set_sync_interval(3600000);

    // 设置时间同步回调
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);

    // 启动SNTP
    esp_sntp_init();

    ESP_LOGI(TAG, "SNTP time sync started with Aliyun NTP servers");
}

/**
 * @brief 获取当前时间字符串
 * @param time_str 输出缓冲区
 * @param max_len 缓冲区最大长度
 * @return 是否成功获取时间
 */
bool wifi_manager_get_time_str(char* time_str, size_t max_len) {
    time_t now = 0;
    struct tm timeinfo = {0};

    // 获取当前时间
    time(&now);
    localtime_r(&now, &timeinfo);

    // 检查时间是否已同步（年份应该大于2020）
    if (timeinfo.tm_year < (2020 - 1900)) {
        snprintf(time_str, max_len, "Syncing...");
        return false;
    }

    // 格式化时间字符串：只显示时间 HH:MM
    strftime(time_str, max_len, "%H:%M", &timeinfo);
    return true;
}

int32_t wifi_manager_get_wifi_list_size(void) { return wifi_list_size; }

const char* wifi_manager_get_wifi_ssid_by_index(int32_t index) {
    if (index < 0 || index >= wifi_list_size) {
        return NULL;
    }
    return wifi_list[index].ssid;
}

const char* wifi_manager_get_saved_password(const char* ssid) {
    return find_wifi_password(ssid);
}

esp_err_t wifi_manager_connect_to_index(int32_t index) {
    if (index < 0 || index >= wifi_list_size) {
        return ESP_ERR_INVALID_ARG;
    }
    return wifi_manager_connect_with_password(wifi_list[index].ssid, wifi_list[index].password);
}

esp_err_t wifi_manager_connect_with_password(const char* ssid, const char* password) {
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Connecting to %s...", ssid);

    // 如果正在扫描，先停止扫描
    if (s_scan_in_progress) {
        ESP_LOGI(TAG, "Stopping ongoing WiFi scan before connecting");
        esp_wifi_scan_stop();
        s_scan_in_progress = false;
    }

    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(200));

    wifi_config_t wifi_config;
    esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get current WiFi config: %s", esp_err_to_name(err));
        memset(&wifi_config, 0, sizeof(wifi_config));
    }

    memset(wifi_config.sta.ssid, 0, sizeof(wifi_config.sta.ssid));
    memset(wifi_config.sta.password, 0, sizeof(wifi_config.sta.password));
    strlcpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    if (password) {
        strlcpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    }

    wifi_config.sta.threshold.authmode = (password && password[0] != '\0') ? WIFI_AUTH_WPA_WPA2_PSK
                                                                            : WIFI_AUTH_OPEN;
    wifi_config.sta.threshold.rssi = -127;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
#ifdef CONFIG_ESP_WIFI_ENABLE_WPA3_SAE
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
#endif

    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi config: %s", esp_err_to_name(err));
        return err;
    }

    // 连接前优先尝试 HT40，如失败则回退 HT20，避免在不支持 HT40 的 AP 上造成不稳定
    do {
        esp_err_t bw_ret = esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT40);
        if (bw_ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to set STA bandwidth to HT40 before connect, fallback HT20: %s", esp_err_to_name(bw_ret));
            bw_ret = esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
            if (bw_ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to set STA bandwidth to HT20 before connect: %s", esp_err_to_name(bw_ret));
            }
        }
    } while (0);

    err = esp_wifi_connect();
    if (err == ESP_OK) {
        g_wifi_info.state = WIFI_STATE_CONNECTING;
        strlcpy(g_wifi_info.ssid, ssid, sizeof(g_wifi_info.ssid));
        add_wifi_to_list(ssid, password);
        if (g_event_cb) {
            g_event_cb();
        }
    } else {
        ESP_LOGE(TAG, "Failed to connect: %s", esp_err_to_name(err));
    }
    return err;
}

wifi_err_reason_t wifi_manager_get_last_disconnect_reason(void) { return s_last_disconnect_reason; }

uint32_t wifi_manager_get_disconnect_sequence(void) { return s_disconnect_sequence; }

static void __attribute__((unused)) save_wifi_config_to_nvs(const char* ssid, const char* password) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_set_str(nvs_handle, WIFI_NVS_KEY_SSID, ssid);
        nvs_set_str(nvs_handle, WIFI_NVS_KEY_PASSWORD, password);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "WiFi config saved to NVS");
    } else {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
    }
}

static bool load_wifi_config_from_nvs(char* ssid, size_t ssid_len, char* password,
                                      size_t password_len) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        err = nvs_get_str(nvs_handle, WIFI_NVS_KEY_SSID, ssid, &ssid_len);
        if (err == ESP_OK) {
            err = nvs_get_str(nvs_handle, WIFI_NVS_KEY_PASSWORD, password, &password_len);
            if (err == ESP_OK) {
                nvs_close(nvs_handle);
                ESP_LOGI(TAG, "WiFi config loaded from NVS");
                return true;
            }
        }
        nvs_close(nvs_handle);
    }
    ESP_LOGW(TAG, "No WiFi config found in NVS");
    return false;
}

static void save_wifi_list_to_nvs() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_set_blob(nvs_handle, "wifi_list", wifi_list, sizeof(wifi_list));
        nvs_set_i32(nvs_handle, "wifi_list_size", wifi_list_size);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "WiFi list saved to NVS");
    } else {
        ESP_LOGE(TAG, "Failed to open NVS for saving WiFi list: %s", esp_err_to_name(err));
    }
}

static void load_wifi_list_from_nvs() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        size_t required_size = sizeof(wifi_list);
        err = nvs_get_blob(nvs_handle, "wifi_list", wifi_list, &required_size);
        if (err == ESP_OK) {
            nvs_get_i32(nvs_handle, "wifi_list_size", &wifi_list_size);
            ESP_LOGI(TAG, "WiFi list loaded from NVS");
        } else {
            ESP_LOGW(TAG, "No WiFi list found in NVS");
        }
        nvs_close(nvs_handle);
    } else {
        ESP_LOGE(TAG, "Failed to open NVS for loading WiFi list: %s", esp_err_to_name(err));
    }
}

static void add_wifi_to_list(const char* ssid, const char* password) {
    if (ssid == NULL || ssid[0] == '\0') {
        return;
    }

    for (int32_t i = 0; i < wifi_list_size; i++) {
        if (strcmp(wifi_list[i].ssid, ssid) == 0) {
            if (password) {
                strlcpy(wifi_list[i].password, password, sizeof(wifi_list[i].password));
            } else {
                wifi_list[i].password[0] = '\0';
            }
            save_wifi_list_to_nvs();
            ESP_LOGI(TAG, "WiFi updated in list: %s", ssid);
            return;
        }
    }

    if (wifi_list_size >= MAX_WIFI_LIST_SIZE) {
        ESP_LOGW(TAG, "WiFi list is full, cannot add: %s", ssid);
        return;
    }

    strlcpy(wifi_list[wifi_list_size].ssid, ssid, sizeof(wifi_list[wifi_list_size].ssid));
    if (password) {
        strlcpy(wifi_list[wifi_list_size].password, password,
                sizeof(wifi_list[wifi_list_size].password));
    } else {
        wifi_list[wifi_list_size].password[0] = '\0';
    }
    wifi_list_size++;
    save_wifi_list_to_nvs();
    ESP_LOGI(TAG, "WiFi added to list: %s", ssid);
}

static const char* find_wifi_password(const char* ssid) {
    if (ssid == NULL) {
        return NULL;
    }
    for (int32_t i = 0; i < wifi_list_size; i++) {
        if (strcmp(wifi_list[i].ssid, ssid) == 0) {
            return wifi_list[i].password;
        }
    }
    return NULL;
}

/**
 * @brief 检查并确保WiFi使用40MHz带宽
 * 
 * 该函数作为定时器回调，定期检查并确保WiFi始终使用40MHz带宽
 * 无论是STA模式还是AP模式
 */
static void check_wifi_bandwidth_timer_cb(TimerHandle_t xTimer) {
    wifi_mode_t mode;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get WiFi mode: %s", esp_err_to_name(err));
        return;
    }
    
    // 检查STA模式
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        wifi_bandwidth_t bw;
        err = esp_wifi_get_bandwidth(WIFI_IF_STA, &bw);
        if (err == ESP_OK && bw != WIFI_BW_HT40) {
            ESP_LOGI(TAG, "STA模式带宽不是40MHz，正在设置为40MHz");
            err = esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT40);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "设置STA模式带宽失败: %s", esp_err_to_name(err));
            } else {
                ESP_LOGI(TAG, "成功设置STA模式带宽为40MHz");
            }
        }
    }
    
    // 检查AP模式
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        wifi_bandwidth_t bw;
        err = esp_wifi_get_bandwidth(WIFI_IF_AP, &bw);
        if (err == ESP_OK && bw != WIFI_BW_HT40) {
            ESP_LOGI(TAG, "AP模式带宽不是40MHz，正在设置为40MHz");
            err = esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT40);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "设置AP模式带宽失败: %s", esp_err_to_name(err));
            } else {
                ESP_LOGI(TAG, "成功设置AP模式带宽为40MHz");
            }
        }
    }
}

/**
 * @brief 启动WiFi带宽检查定时器
 */
static void start_wifi_bandwidth_check_timer(void) {
    // 如果定时器已存在，先删除
    if (wifi_bandwidth_check_timer != NULL) {
        xTimerDelete(wifi_bandwidth_check_timer, 0);
    }
    
    // 创建定时器，每隔一段时间检查一次带宽
    wifi_bandwidth_check_timer = xTimerCreate(
        "wifi_bw_check",
        pdMS_TO_TICKS(WIFI_BW_CHECK_INTERVAL_MS),
        pdTRUE,  // 自动重载
        NULL,
        check_wifi_bandwidth_timer_cb
    );
    
    if (wifi_bandwidth_check_timer != NULL) {
        if (xTimerStart(wifi_bandwidth_check_timer, 0) != pdPASS) {
            ESP_LOGE(TAG, "Failed to start WiFi bandwidth check timer");
        } else {
            ESP_LOGI(TAG, "WiFi bandwidth check timer started");
        }
    } else {
        ESP_LOGE(TAG, "Failed to create WiFi bandwidth check timer");
    }
}

esp_err_t wifi_manager_start_scan(bool block) {
    if (s_scan_in_progress) {
        ESP_LOGW(TAG, "WiFi scan already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get WiFi mode: %s", esp_err_to_name(err));
        return err;
    }

    if (mode != WIFI_MODE_STA && mode != WIFI_MODE_APSTA) {
        ESP_LOGW(TAG, "WiFi scan requested while STA mode is not active");
        return ESP_ERR_INVALID_STATE;
    }

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    scan_config.scan_time.active.min = 100;
    scan_config.scan_time.active.max = 300;

    s_scan_in_progress = true;
    s_scan_count = 0;
    s_scan_results_version++;

    err = esp_wifi_scan_start(&scan_config, block);
    if (err != ESP_OK) {
        s_scan_in_progress = false;
        ESP_LOGE(TAG, "Failed to start WiFi scan: %s", esp_err_to_name(err));
        return err;
    }

    if (block) {
        s_scan_in_progress = false;
    }

    ESP_LOGI(TAG, "WiFi scan started (block=%d)", block);
    return ESP_OK;
}

bool wifi_manager_is_scanning(void) { return s_scan_in_progress; }

size_t wifi_manager_get_scan_result_count(void) { return s_scan_count; }

size_t wifi_manager_get_scan_results(wifi_manager_scan_result_t* results, size_t max_results) {
    if (results == NULL || max_results == 0) {
        return 0;
    }

    size_t available = s_scan_count;
    if (available > max_results) {
        available = max_results;
    }

    for (size_t i = 0; i < available; i++) {
        strlcpy(results[i].ssid, (const char*)s_scan_records[i].ssid, sizeof(results[i].ssid));
        results[i].rssi = s_scan_records[i].rssi;
        results[i].authmode = s_scan_records[i].authmode;
        results[i].channel = s_scan_records[i].primary;
    }

    return available;
}

uint32_t wifi_manager_get_scan_results_version(void) { return s_scan_results_version; }