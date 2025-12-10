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

// Global variables to store WiFi info and callback
static wifi_manager_info_t g_wifi_info;
static wifi_manager_event_cb_t g_event_cb = NULL;

#define WIFI_MANAGER_MAX_SCAN_RESULTS 32
static wifi_ap_record_t s_scan_records[WIFI_MANAGER_MAX_SCAN_RESULTS];
static uint16_t s_scan_count = 0;
static bool s_scan_in_progress = false;
static uint32_t s_scan_results_version = 0;
static wifi_err_reason_t s_last_disconnect_reason = WIFI_REASON_UNSPECIFIED;
static uint32_t s_disconnect_sequence = 0;

// Timer for periodic WiFi bandwidth checking
static TimerHandle_t wifi_bandwidth_check_timer = NULL;
static const int WIFI_BW_CHECK_INTERVAL_MS = 30000; // 30 seconds check interval

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

static wifi_config_entry_t wifi_list[MAX_WIFI_LIST_SIZE] = {0};
static int32_t wifi_list_size = 0;
static uint32_t wifi_list_version = 0;

// 前向声明
static bool load_wifi_config_from_nvs(char* ssid, size_t ssid_len, char* password,
                                      size_t password_len);
static void save_wifi_config_to_nvs(const char* ssid, const char* password);
static void save_wifi_list_to_nvs(void);
static void load_wifi_list_from_nvs(void);

/**
 * @brief WiFi event handler
 */
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id,
                          void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        g_wifi_info.state = WIFI_STATE_CONNECTING;
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* disconnected = (wifi_event_sta_disconnected_t*)event_data;
        g_wifi_info.state = WIFI_STATE_DISCONNECTED;
        strcpy(g_wifi_info.ip_addr, "N/A");
        memset(g_wifi_info.ssid, 0, sizeof(g_wifi_info.ssid));

        s_retry_num = 0;
        if (disconnected) {
            s_last_disconnect_reason = disconnected->reason;
        } else {
            s_last_disconnect_reason = WIFI_REASON_UNSPECIFIED;
        }
        s_disconnect_sequence++;

        if (g_event_cb) {
            g_event_cb();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        s_scan_in_progress = false;
        wifi_event_sta_scan_done_t* scan_done = (wifi_event_sta_scan_done_t*)event_data;
        if (scan_done && scan_done->status != 0) {
            s_scan_count = 0;
            s_scan_results_version++;
        } else {
            uint16_t number = WIFI_MANAGER_MAX_SCAN_RESULTS;
            esp_err_t err = esp_wifi_scan_get_ap_records(&number, s_scan_records);
            if (err == ESP_OK) {
                s_scan_count = number;
                s_scan_results_version++;
            } else {
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

        wifi_config_t wifi_config;
        esp_wifi_get_config(WIFI_IF_STA, &wifi_config);
        strncpy(g_wifi_info.ssid, (char*)wifi_config.sta.ssid, sizeof(g_wifi_info.ssid) - 1);

        add_wifi_to_list((char*)wifi_config.sta.ssid, (char*)wifi_config.sta.password);
        save_wifi_config_to_nvs((char*)wifi_config.sta.ssid, (char*)wifi_config.sta.password);

        wifi_manager_sync_time();
    }

    if (g_event_cb) {
        g_event_cb();
    }
}

/**
 * @brief Initialize WiFi stack (NVS, Netif, Event Loop)
 */
static esp_err_t wifi_init_stack(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());

    ret = esp_event_loop_create_default();
    if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Event loop already exists");
    } else {
        ESP_ERROR_CHECK(ret);
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

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

    esp_err_t ret = wifi_init_stack();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_wifi_get_mac(ESP_IF_WIFI_STA, g_wifi_info.mac_addr);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get MAC address");
    }

    return ESP_OK;
}

esp_err_t wifi_manager_start(void) {
    s_wifi_event_group = xEventGroupCreate();

    load_wifi_list_from_nvs();

    wifi_config_t wifi_config = {0};
    bool config_loaded = false;

    if (load_wifi_config_from_nvs((char*)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid),
                                  (char*)wifi_config.sta.password,
                                  sizeof(wifi_config.sta.password))) {
        config_loaded = true;
    } else if (wifi_list_size > 0) {
        strncpy((char*)wifi_config.sta.ssid, wifi_list[0].ssid, sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char*)wifi_config.sta.password, wifi_list[0].password,
                sizeof(wifi_config.sta.password) - 1);
        config_loaded = true;
    } else {
        strcpy((char*)wifi_config.sta.ssid, "");
        strcpy((char*)wifi_config.sta.password, "");
        config_loaded = false;
    }

    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_err_t bandwidth_ret = esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT40);
    if (bandwidth_ret != ESP_OK) {
        bandwidth_ret = esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
        if (bandwidth_ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to set WiFi bandwidth");
        }
    }

    esp_err_t power_ret = wifi_manager_set_power(20);
    if (power_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set WiFi power");
    }

    return ESP_OK;
}

esp_err_t wifi_manager_stop(void) {
    esp_err_t err = esp_wifi_stop();
    if (err == ESP_OK) {
        g_wifi_info.state = WIFI_STATE_DISABLED;
        strcpy(g_wifi_info.ip_addr, "N/A");
        memset(g_wifi_info.ssid, 0, sizeof(g_wifi_info.ssid));
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
    return err;
}

esp_err_t wifi_manager_set_power(int8_t power_dbm) {
    if (power_dbm < 2 || power_dbm > 20) {
        return ESP_ERR_INVALID_ARG;
    }
    int8_t power_val = (int8_t)(power_dbm * 4);
    esp_err_t err = esp_wifi_set_max_tx_power(power_val);
    return err;
}

esp_err_t wifi_manager_get_power(int8_t* power_dbm) {
    if (power_dbm == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    int8_t power_val = 0;
    esp_err_t err = esp_wifi_get_max_tx_power(&power_val);
    if (err == ESP_OK) {
        *power_dbm = power_val / 4;
    }
    return err;
}

wifi_manager_info_t wifi_manager_get_info(void) { return g_wifi_info; }

void wifi_manager_register_event_callback(wifi_manager_event_cb_t event_cb) {
    g_event_cb = event_cb;
}

/**
 * @brief Time sync notification callback
 */
static void time_sync_notification_cb(struct timeval* tv) {}

/**
 * @brief Start time synchronization
 */
void wifi_manager_sync_time(void) {
    setenv("TZ", "CST-8", 1);
    tzset();

    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_setservername(1, "ntp1.aliyun.com");
    esp_sntp_setservername(2, "ntp2.aliyun.com");

    esp_sntp_set_sync_interval(3600000);
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();
}

/**
 * @brief Get current time string
 * @param time_str Output buffer
 * @param max_len Buffer max length
 * @return True if time is synced
 */
bool wifi_manager_get_time_str(char* time_str, size_t max_len) {
    time_t now = 0;
    struct tm timeinfo = {0};

    time(&now);
    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_year < (2020 - 1900)) {
        snprintf(time_str, max_len, "Syncing...");
        return false;
    }

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

uint32_t wifi_manager_get_wifi_list_version(void) {
    return wifi_list_version;
}

esp_err_t wifi_manager_remove_wifi_from_list(const char* ssid) {
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    
    for (int32_t i = 0; i < wifi_list_size; i++) {
        if (strcmp(wifi_list[i].ssid, ssid) == 0) {
            for (int32_t j = i; j < wifi_list_size - 1; j++) {
                memcpy(&wifi_list[j], &wifi_list[j + 1], sizeof(wifi_config_entry_t));
            }
            wifi_list_size--;
            wifi_list_version++;
            save_wifi_list_to_nvs();
            return ESP_OK;
        }
    }
    
    return ESP_ERR_NOT_FOUND;
}

esp_err_t wifi_manager_clear_wifi_list(void) {
    wifi_list_size = 0;
    wifi_list_version++;
    memset(wifi_list, 0, sizeof(wifi_list));
    save_wifi_list_to_nvs();
    return ESP_OK;
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

    if (s_scan_in_progress) {
        esp_wifi_scan_stop();
        s_scan_in_progress = false;
    }

    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(200));

    wifi_config_t wifi_config;
    esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
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

    esp_err_t bw_ret = esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT40);
    if (bw_ret != ESP_OK) {
        bw_ret = esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
        if (bw_ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to set bandwidth");
        }
    }

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
    } else {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
    }
}

static bool load_wifi_config_from_nvs(char* ssid, size_t ssid_len, char* password,
                                      size_t password_len) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        size_t actual_ssid_len = ssid_len;
        size_t actual_password_len = password_len;
        err = nvs_get_str(nvs_handle, WIFI_NVS_KEY_SSID, ssid, &actual_ssid_len);
        if (err == ESP_OK) {
            err = nvs_get_str(nvs_handle, WIFI_NVS_KEY_PASSWORD, password, &actual_password_len);
            if (err == ESP_OK) {
                nvs_close(nvs_handle);
                return true;
            }
        }
        nvs_close(nvs_handle);
    }
    return false;
}

static void save_wifi_list_to_nvs() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(nvs_handle, "wifi_list", wifi_list, 
                          (size_t)wifi_list_size * sizeof(wifi_config_entry_t));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set WiFi list blob: %s", esp_err_to_name(err));
            nvs_close(nvs_handle);
            return;
        }
        
        err = nvs_set_i32(nvs_handle, "wifi_list_size", wifi_list_size);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set WiFi list size: %s", esp_err_to_name(err));
            nvs_close(nvs_handle);
            return;
        }
        
        err = nvs_set_u32(nvs_handle, "wifi_list_ver", wifi_list_version);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set WiFi list version: %s", esp_err_to_name(err));
        }
        
        err = nvs_commit(nvs_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
        }
        nvs_close(nvs_handle);
    } else {
        ESP_LOGE(TAG, "Failed to open NVS for saving WiFi list: %s", esp_err_to_name(err));
    }
}

static void load_wifi_list_from_nvs() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    
    wifi_list_size = 0;
    wifi_list_version = 0;
    memset(wifi_list, 0, sizeof(wifi_list));
    
    if (err == ESP_OK) {
        int32_t saved_size = 0;
        esp_err_t size_err = nvs_get_i32(nvs_handle, "wifi_list_size", &saved_size);
        
        if (size_err == ESP_OK && saved_size > 0 && saved_size <= MAX_WIFI_LIST_SIZE) {
            uint32_t saved_version = 0;
            nvs_get_u32(nvs_handle, "wifi_list_ver", &saved_version);
            
            size_t required_size = (size_t)saved_size * sizeof(wifi_config_entry_t);
            err = nvs_get_blob(nvs_handle, "wifi_list", wifi_list, &required_size);
            
            if (err == ESP_OK) {
                wifi_list_size = saved_size;
                wifi_list_version = saved_version;
            }
        }
        nvs_close(nvs_handle);
    }
}

static void add_wifi_to_list(const char* ssid, const char* password) {
    if (ssid == NULL || ssid[0] == '\0') {
        return;
    }

    for (int32_t i = 0; i < wifi_list_size; i++) {
        if (strcmp(wifi_list[i].ssid, ssid) == 0) {
            if (password) {
                size_t pwd_len = strlen(password);
                if (pwd_len > sizeof(wifi_list[i].password) - 1) {
                    strlcpy(wifi_list[i].password, password, sizeof(wifi_list[i].password));
                } else {
                    strlcpy(wifi_list[i].password, password, sizeof(wifi_list[i].password));
                }
            } else {
                wifi_list[i].password[0] = '\0';
            }
            
            wifi_list_version++;
            save_wifi_list_to_nvs();
            return;
        }
    }

    if (wifi_list_size >= MAX_WIFI_LIST_SIZE) {
        return;
    }

    size_t ssid_len = strlen(ssid);
    if (ssid_len > sizeof(wifi_list[wifi_list_size].ssid) - 1) {
        strlcpy(wifi_list[wifi_list_size].ssid, ssid, sizeof(wifi_list[wifi_list_size].ssid));
    } else {
        strlcpy(wifi_list[wifi_list_size].ssid, ssid, sizeof(wifi_list[wifi_list_size].ssid));
    }
    
    if (password) {
        size_t pwd_len = strlen(password);
        if (pwd_len > sizeof(wifi_list[wifi_list_size].password) - 1) {
            strlcpy(wifi_list[wifi_list_size].password, password, 
                   sizeof(wifi_list[wifi_list_size].password));
        } else {
            strlcpy(wifi_list[wifi_list_size].password, password,
                   sizeof(wifi_list[wifi_list_size].password));
        }
    } else {
        wifi_list[wifi_list_size].password[0] = '\0';
    }
    
    wifi_list_size++;
    wifi_list_version++;
    save_wifi_list_to_nvs();
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
 * @brief Check and ensure WiFi uses 40MHz bandwidth
 */
static void check_wifi_bandwidth_timer_cb(TimerHandle_t xTimer) {
    wifi_mode_t mode;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) {
        return;
    }
    
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        wifi_bandwidth_t bw;
        err = esp_wifi_get_bandwidth(WIFI_IF_STA, &bw);
        if (err == ESP_OK && bw != WIFI_BW_HT40) {
            esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT40);
        }
    }
    
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        wifi_bandwidth_t bw;
        err = esp_wifi_get_bandwidth(WIFI_IF_AP, &bw);
        if (err == ESP_OK && bw != WIFI_BW_HT40) {
            esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT40);
        }
    }
}

/**
 * @brief Start WiFi bandwidth check timer
 */
static void start_wifi_bandwidth_check_timer(void) {
    if (wifi_bandwidth_check_timer != NULL) {
        xTimerDelete(wifi_bandwidth_check_timer, 0);
    }
    
    wifi_bandwidth_check_timer = xTimerCreate(
        "wifi_bw_check",
        pdMS_TO_TICKS(WIFI_BW_CHECK_INTERVAL_MS),
        pdTRUE,
        NULL,
        check_wifi_bandwidth_timer_cb
    );
    
    if (wifi_bandwidth_check_timer != NULL) {
        if (xTimerStart(wifi_bandwidth_check_timer, 0) != pdPASS) {
            ESP_LOGE(TAG, "Failed to start bandwidth check timer");
        }
    } else {
        ESP_LOGE(TAG, "Failed to create bandwidth check timer");
    }
}

esp_err_t wifi_manager_start_scan(bool block) {
    if (s_scan_in_progress) {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) {
        return err;
    }

    if (mode != WIFI_MODE_STA && mode != WIFI_MODE_APSTA) {
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