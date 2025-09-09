#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "led_status_manager.h"
#include "wifi_pairing_manager.h"

static const char* TAG = "WifiAutoPairing";

// NVS存储键名
#define NVS_NAMESPACE "wifi_pairing"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASSWORD "password"
#define NVS_KEY_VALID "valid"

// WiFi事件位
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

// 管理器状态
static bool s_initialized = false;
static bool s_running = false;
static TaskHandle_t s_scan_task_handle = NULL;
static TimerHandle_t s_scan_timer = NULL;
static EventGroupHandle_t s_wifi_event_group = NULL;
static esp_netif_t* s_sta_netif = NULL;

// 当前状态和配置
static wifi_pairing_state_t s_current_state = WIFI_PAIRING_STATE_IDLE;
static wifi_pairing_config_t s_config = {0};
static wifi_pairing_event_cb_t s_event_callback = NULL;
static wifi_credentials_t s_current_credentials = {0};
static int s_retry_count = 0;
static const int s_max_retry = 3;

// 内部函数声明
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id,
                               void* event_data);
static void scan_timer_callback(TimerHandle_t xTimer);
static void wifi_scan_task(void* pvParameters);
static esp_err_t save_credentials_to_nvs(const wifi_credentials_t* credentials);
static esp_err_t load_credentials_from_nvs(wifi_credentials_t* credentials);
static esp_err_t connect_to_wifi(const char* ssid, const char* password);
static esp_err_t scan_and_connect_target(void);
static void set_state_and_notify(wifi_pairing_state_t new_state, const char* ssid);
static void update_led_status(wifi_pairing_state_t state);
static bool is_target_ssid(const char* ssid);
static void get_mac_suffix(char* mac_suffix, size_t size);
static void start_scan_timer(void);
static void stop_scan_timer(void);

/**
 * @brief 初始化WiFi配对管理器
 */
esp_err_t wifi_pairing_manager_init(const wifi_pairing_config_t* config,
                                    wifi_pairing_event_cb_t event_cb) {
    if (s_initialized) {
        ESP_LOGW(TAG, "WiFi配对管理器已初始化");
        return ESP_OK;
    }

    if (!config) {
        ESP_LOGE(TAG, "配置参数为空");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "初始化WiFi配对管理器...");

    // 初始化网络接口
    esp_err_t netif_ret = esp_netif_init();
    if (netif_ret != ESP_OK) {
        ESP_LOGE(TAG, "网络接口初始化失败: %s", esp_err_to_name(netif_ret));
        return netif_ret;
    }
    ESP_LOGI(TAG, "网络接口初始化完成");

    // 初始化事件循环
    esp_err_t event_ret = esp_event_loop_create_default();
    if (event_ret != ESP_OK) {
        ESP_LOGE(TAG, "事件循环初始化失败: %s", esp_err_to_name(event_ret));
        return event_ret;
    }
    ESP_LOGI(TAG, "事件循环初始化完成");

    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_sta_netif) {
        ESP_LOGE(TAG, "创建WiFi网络接口失败");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "初始化默认WiFi网络接口");

    // 初始化WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t wifi_ret = esp_wifi_init(&cfg);
    if (wifi_ret != ESP_OK && wifi_ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "WiFi初始化失败: %s", esp_err_to_name(wifi_ret));
        return wifi_ret;
    }
    ESP_LOGI(TAG, "WiFi初始化完成");

    // 保存配置和回调
    memcpy(&s_config, config, sizeof(wifi_pairing_config_t));
    s_event_callback = event_cb;
    ESP_LOGI(TAG, "保存配置和回调");

    // 创建WiFi事件组
    s_wifi_event_group = xEventGroupCreate();
    if (!s_wifi_event_group) {
        ESP_LOGE(TAG, "创建WiFi事件组失败");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "创建WiFi事件组");

    // 注册WiFi事件处理器
    esp_err_t wifi_event_ret =
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    if (wifi_event_ret != ESP_OK && wifi_event_ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "注册WiFi事件处理器失败: %s", esp_err_to_name(wifi_event_ret));
        return wifi_event_ret;
    }

    esp_err_t ip_event_ret =
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);
    if (ip_event_ret != ESP_OK && ip_event_ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "注册IP事件处理器失败: %s", esp_err_to_name(ip_event_ret));
        return ip_event_ret;
    }
    ESP_LOGI(TAG, "WiFi事件处理器注册完成");

    // 设置WiFi模式
    esp_err_t mode_ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (mode_ret != ESP_OK) {
        ESP_LOGE(TAG, "设置WiFi模式失败: %s", esp_err_to_name(mode_ret));
        return mode_ret;
    }

    esp_err_t start_ret = esp_wifi_start();
    if (start_ret != ESP_OK && start_ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "启动WiFi失败: %s", esp_err_to_name(start_ret));
        return start_ret;
    }
    ESP_LOGI(TAG, "WiFi模式设置和启动完成");

    // 创建扫描定时器
    s_scan_timer = xTimerCreate("scan_timer", pdMS_TO_TICKS(s_config.scan_interval_ms), pdTRUE,
                                (void*)0, scan_timer_callback);
    if (!s_scan_timer) {
        ESP_LOGE(TAG, "创建扫描定时器失败");
        vEventGroupDelete(s_wifi_event_group);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "创建扫描定时器");

    s_initialized = true;
    s_current_state = WIFI_PAIRING_STATE_IDLE;

    ESP_LOGI(TAG, "WiFi配对管理器初始化成功");
    return ESP_OK;
}

/**
 * @brief 反初始化WiFi配对管理器
 */
esp_err_t wifi_pairing_manager_deinit(void) {
    if (!s_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "反初始化WiFi配对管理器...");

    // 停止管理器
    wifi_pairing_manager_stop();

    // 删除定时器
    if (s_scan_timer) {
        xTimerDelete(s_scan_timer, portMAX_DELAY);
        s_scan_timer = NULL;
    }

    // 注销事件处理器
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler);

    // 停止WiFi
    esp_wifi_stop();
    esp_wifi_deinit();

    // 删除事件组
    if (s_wifi_event_group) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }

    // 销毁网络接口
    if (s_sta_netif) {
        esp_netif_destroy(s_sta_netif);
        s_sta_netif = NULL;
    }

    s_initialized = false;
    s_current_state = WIFI_PAIRING_STATE_IDLE;

    ESP_LOGI(TAG, "WiFi配对管理器反初始化完成");
    return ESP_OK;
}

/**
 * @brief 启动WiFi配对管理器
 */
esp_err_t wifi_pairing_manager_start(void) {
    if (!s_initialized) {
        ESP_LOGE(TAG, "WiFi配对管理器未初始化");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_running) {
        ESP_LOGW(TAG, "WiFi配对管理器已在运行");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "启动WiFi配对管理器...");

    // 首先尝试从NVS加载凭证
    wifi_credentials_t stored_credentials = {0};

    // 让出CPU
    vTaskDelay(pdMS_TO_TICKS(10));

    esp_err_t ret = load_credentials_from_nvs(&stored_credentials);

    // 让出CPU
    vTaskDelay(pdMS_TO_TICKS(10));

    if (ret == ESP_OK && stored_credentials.valid) {
        ESP_LOGI(TAG, "发现存储的WiFi凭证，尝试连接到: %s", stored_credentials.ssid);
        memcpy(&s_current_credentials, &stored_credentials, sizeof(wifi_credentials_t));

        // 设置连接状态并更新LED
        set_state_and_notify(WIFI_PAIRING_STATE_CONNECTING, stored_credentials.ssid);

        // 尝试连接
        ret = connect_to_wifi(stored_credentials.ssid, stored_credentials.password);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "连接存储的WiFi失败，开始扫描模式");
            set_state_and_notify(WIFI_PAIRING_STATE_SCANNING, NULL);
        }
    } else {
        ESP_LOGI(TAG, "未发现存储的WiFi凭证，开始扫描模式");
        set_state_and_notify(WIFI_PAIRING_STATE_SCANNING, NULL);
    }

    // 启动扫描定时器
    start_scan_timer();

    s_running = true;
    ESP_LOGI(TAG, "WiFi配对管理器启动成功");
    return ESP_OK;
}

/**
 * @brief 停止WiFi配对管理器
 */
esp_err_t wifi_pairing_manager_stop(void) {
    if (!s_running) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "停止WiFi配对管理器...");

    // 停止定时器
    if (s_scan_timer) {
        xTimerStop(s_scan_timer, portMAX_DELAY);
    }

    // 删除扫描任务
    if (s_scan_task_handle) {
        vTaskDelete(s_scan_task_handle);
        s_scan_task_handle = NULL;
    }

    // 断开WiFi连接
    esp_wifi_disconnect();

    s_running = false;
    set_state_and_notify(WIFI_PAIRING_STATE_IDLE, NULL);

    ESP_LOGI(TAG, "WiFi配对管理器停止完成");
    return ESP_OK;
}

/**
 * @brief 获取当前WiFi配对状态
 */
wifi_pairing_state_t wifi_pairing_get_state(void) { return s_current_state; }

/**
 * @brief 获取当前连接的WiFi信息
 */
esp_err_t wifi_pairing_get_current_credentials(wifi_credentials_t* credentials) {
    if (!credentials) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(credentials, &s_current_credentials, sizeof(wifi_credentials_t));
    return ESP_OK;
}

/**
 * @brief 清除存储的WiFi凭证
 */
esp_err_t wifi_pairing_clear_credentials(void) {
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "打开NVS失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 删除所有相关键
    nvs_erase_key(nvs_handle, NVS_KEY_SSID);
    nvs_erase_key(nvs_handle, NVS_KEY_PASSWORD);
    nvs_erase_key(nvs_handle, NVS_KEY_VALID);

    ret = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    // 清除内存中的凭证
    memset(&s_current_credentials, 0, sizeof(wifi_credentials_t));

    ESP_LOGI(TAG, "WiFi凭证已清除");
    return ret;
}

/**
 * @brief 手动触发WiFi扫描
 */
esp_err_t wifi_pairing_trigger_scan(void) {
    if (!s_initialized || !s_running) {
        return ESP_ERR_INVALID_STATE;
    }

    // 创建扫描任务
    if (s_scan_task_handle == NULL) {
        BaseType_t ret = xTaskCreate(wifi_scan_task, "wifi_scan", s_config.task_stack_size, NULL,
                                     s_config.task_priority, &s_scan_task_handle);
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "创建WiFi扫描任务失败");
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
}

/**
 * @brief 检查WiFi配对管理器是否已初始化
 */
bool wifi_pairing_manager_is_initialized(void) { return s_initialized; }

/**
 * @brief 检查WiFi配对管理器是否正在运行
 */
bool wifi_pairing_manager_is_running(void) { return s_running; }

// ========== 内部函数实现 ==========

/**
 * @brief WiFi事件处理器
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id,
                               void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi STA启动");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*)event_data;
        ESP_LOGI(TAG, "WiFi断开连接，原因: %d", event->reason);

        if (s_current_state == WIFI_PAIRING_STATE_CONNECTING && s_retry_count < s_max_retry) {
            esp_wifi_connect();
            s_retry_count++;
            ESP_LOGI(TAG, "重试连接WiFi (%d/%d)", s_retry_count, s_max_retry);
        } else {
            set_state_and_notify(WIFI_PAIRING_STATE_DISCONNECTED, NULL);
            s_retry_count = 0;
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            // 连接失败或断开后，重新开始扫描
            start_scan_timer();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "获得IP地址: " IPSTR, IP2STR(&event->ip_info.ip));

        set_state_and_notify(WIFI_PAIRING_STATE_CONNECTED, s_current_credentials.ssid);
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        // 保存成功连接的凭证
        save_credentials_to_nvs(&s_current_credentials);

        // 连接成功，停止扫描定时器
        stop_scan_timer();
    }
}

/**
 * @brief 扫描定时器回调
 */
static void scan_timer_callback(TimerHandle_t xTimer) {
    if (s_current_state == WIFI_PAIRING_STATE_SCANNING) {
        wifi_pairing_trigger_scan();
    }
}

/**
 * @brief WiFi扫描任务
 */
static void wifi_scan_task(void* pvParameters) {
    ESP_LOGI(TAG, "开始WiFi扫描任务");

    esp_err_t ret = scan_and_connect_target();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "扫描和连接失败: %s", esp_err_to_name(ret));
    }

    // 任务完成，清除句柄
    s_scan_task_handle = NULL;
    vTaskDelete(NULL);
}

/**
 * @brief 保存凭证到NVS
 */
static esp_err_t save_credentials_to_nvs(const wifi_credentials_t* credentials) {
    if (!credentials || !credentials->valid) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "打开NVS失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 保存SSID
    ret = nvs_set_str(nvs_handle, NVS_KEY_SSID, credentials->ssid);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "保存SSID失败: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }

    // 保存密码
    ret = nvs_set_str(nvs_handle, NVS_KEY_PASSWORD, credentials->password);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "保存密码失败: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }

    // 保存有效标志
    ret = nvs_set_u8(nvs_handle, NVS_KEY_VALID, credentials->valid ? 1 : 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "保存有效标志失败: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }

    ret = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi凭证已保存到NVS: %s", credentials->ssid);
    }

    return ret;
}

/**
 * @brief 从NVS加载凭证
 */
static esp_err_t load_credentials_from_nvs(wifi_credentials_t* credentials) {
    if (!credentials) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        // NVS打开失败
        return ret;
    }

    // 初始化结构体
    memset(credentials, 0, sizeof(wifi_credentials_t));

    // 读取有效标志
    uint8_t valid_flag = 0;
    ret = nvs_get_u8(nvs_handle, NVS_KEY_VALID, &valid_flag);
    if (ret != ESP_OK) {
        // 读取NVS标志失败
        nvs_close(nvs_handle);
        return ret;
    }

    if (valid_flag == 0) {
        // NVS凭证无效
        nvs_close(nvs_handle);
        return ESP_ERR_NOT_FOUND;
    }

    // 读取SSID
    size_t ssid_len = sizeof(credentials->ssid);
    ret = nvs_get_str(nvs_handle, NVS_KEY_SSID, credentials->ssid, &ssid_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "读取SSID失败: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }

    // 读取密码
    size_t password_len = sizeof(credentials->password);
    ret = nvs_get_str(nvs_handle, NVS_KEY_PASSWORD, credentials->password, &password_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "读取密码失败: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }

    credentials->valid = true;
    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "从NVS加载WiFi凭证成功: %s", credentials->ssid);
    return ESP_OK;
}

/**
 * @brief 连接到WiFi
 */
static esp_err_t connect_to_wifi(const char* ssid, const char* password) {
    if (!ssid || !password) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_LOGI(TAG, "连接到WiFi: %s", ssid);

    // 让出CPU以避免看门狗重启
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // 让出CPU
    vTaskDelay(pdMS_TO_TICKS(10));

    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi连接失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 让出CPU，让连接过程有时间进行
    vTaskDelay(pdMS_TO_TICKS(100));

    // 更新当前凭证
    strncpy(s_current_credentials.ssid, ssid, sizeof(s_current_credentials.ssid) - 1);
    strncpy(s_current_credentials.password, password, sizeof(s_current_credentials.password) - 1);
    s_current_credentials.valid = true;

    return ESP_OK;
}

/**
 * @brief 扫描并连接目标WiFi
 */
static esp_err_t scan_and_connect_target(void) {
    ESP_LOGI(TAG, "开始扫描WiFi网络...");

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    // 让出CPU以避免看门狗重启
    vTaskDelay(pdMS_TO_TICKS(10));

    esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi扫描失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 等待扫描完成，期间让出CPU
    vTaskDelay(pdMS_TO_TICKS(500)); // 给扫描一些时间

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);

    if (ap_count == 0) {
        ESP_LOGW(TAG, "未发现任何WiFi网络");
        return ESP_ERR_NOT_FOUND;
    }

    wifi_ap_record_t* ap_list = malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (!ap_list) {
        ESP_LOGE(TAG, "分配内存失败");
        return ESP_ERR_NO_MEM;
    }

    ret = esp_wifi_scan_get_ap_records(&ap_count, ap_list);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "获取扫描结果失败: %s", esp_err_to_name(ret));
        free(ap_list);
        return ret;
    }

    ESP_LOGI(TAG, "发现 %d 个WiFi网络", ap_count);

    // 查找目标SSID
    bool found_target = false;
    for (int i = 0; i < ap_count; i++) {
        // 发现WiFi网络

        if (is_target_ssid((char*)ap_list[i].ssid)) {
            ESP_LOGI(TAG, "发现目标WiFi: %s", ap_list[i].ssid);

            // 设置连接状态
            set_state_and_notify(WIFI_PAIRING_STATE_CONNECTING, (char*)ap_list[i].ssid);

            // 尝试连接
            ret = connect_to_wifi((char*)ap_list[i].ssid, s_config.default_password);
            if (ret == ESP_OK) {
                found_target = true;
            }
            break;
        }
    }

    free(ap_list);

    if (!found_target) {
        // 未发现目标网络
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}

/**
 * @brief 设置状态并通知
 */
static void set_state_and_notify(wifi_pairing_state_t new_state, const char* ssid) {
    if (s_current_state != new_state) {
        s_current_state = new_state;
        update_led_status(new_state);

        if (s_event_callback) {
            s_event_callback(new_state, ssid);
        }

        ESP_LOGI(TAG, "状态变更: %d", new_state);
    }
}

/**
 * @brief 更新LED状态
 */
static void update_led_status(wifi_pairing_state_t state) {
    switch (state) {
    case WIFI_PAIRING_STATE_CONNECTING:
        // 连接过程中显示蓝红闪烁
        led_status_set_style(LED_STYLE_BLUE_RED_BLINK, LED_PRIORITY_NORMAL, 0);
        break;

    case WIFI_PAIRING_STATE_CONNECTED:
        // 连接成功显示绿色呼吸
        led_status_set_style(LED_STYLE_GREEN_BREATHING, LED_PRIORITY_NORMAL, 0);
        break;

    case WIFI_PAIRING_STATE_DISCONNECTED:
    case WIFI_PAIRING_STATE_ERROR:
        // 断开连接或错误时显示红色常亮
        led_status_set_style(LED_STYLE_RED_SOLID, LED_PRIORITY_NORMAL, 0);
        break;

    default:
        // 其他状态保持当前LED状态
        break;
    }
}

/**
 * @brief 启动扫描定时器
 */
static void start_scan_timer(void) {
    if (s_scan_timer && !xTimerIsTimerActive(s_scan_timer)) {
        if (xTimerStart(s_scan_timer, portMAX_DELAY) != pdPASS) {
            ESP_LOGE(TAG, "启动扫描定时器失败");
        } else {
            ESP_LOGI(TAG, "扫描定时器已启动");
        }
    }
}

/**
 * @brief 停止扫描定时器
 */
static void stop_scan_timer(void) {
    if (s_scan_timer && xTimerIsTimerActive(s_scan_timer)) {
        if (xTimerStop(s_scan_timer, portMAX_DELAY) != pdPASS) {
            ESP_LOGE(TAG, "停止扫描定时器失败");
        } else {
            ESP_LOGI(TAG, "扫描定时器已停止");
        }
    }
}

/**
 * @brief 检查是否为目标SSID
 */
static bool is_target_ssid(const char* ssid) {
    if (!ssid) {
        return false;
    }

    // 检查前缀
    if (strncmp(ssid, s_config.target_ssid_prefix, strlen(s_config.target_ssid_prefix)) != 0) {
        return false;
    }

    // 检查MAC地址后缀格式（简单验证）
    const char* mac_part = ssid + strlen(s_config.target_ssid_prefix);
    if (strlen(mac_part) != 4) { // 期望4位MAC地址后缀
        return false;
    }

    // 检查是否为十六进制字符
    for (int i = 0; i < 4; i++) {
        char c = mac_part[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }

    return true;
}

/**
 * @brief 获取MAC地址后缀
 */
static void get_mac_suffix(char* mac_suffix, size_t size) {
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(mac_suffix, size, "%02X%02X", mac[4], mac[5]);
}