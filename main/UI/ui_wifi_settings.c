#include "esp_log.h" // 引入日志功能
#include "ui.h"
#include "ui_numeric_keypad.h"
#include "wifi_manager.h" 
#include <stdio.h>
#include <string.h>       
#include <stdint.h>

// 为了实现返回功能，需要前向声明设置菜单的创建函数
void ui_settings_create(lv_obj_t* parent);

// 语言文本定义
typedef struct {
    const char* title;
    const char* enable_wifi;
    const char* tx_power;
    const char* saved_networks;
    const char* available_networks;
    const char* refresh_button;
    const char* scan_in_progress;
    const char* scan_no_networks;
    const char* scan_result_fmt;
    const char* details_button;
    const char* details_title;
    const char* status_label;
    const char* ssid_label;
    const char* ip_label;
    const char* mac_label;
    const char* status_disabled;
    const char* status_disconnected;
    const char* status_connecting;
    const char* status_connected;
    const char* keypad_title_fmt;
    const char* saved_tag;
    const char* connect_failed_fmt;
    const char* reason_auth_fail;
    const char* reason_no_ap;
    const char* reason_assoc_fail;
    const char* reason_handshake_timeout;
    const char* reason_unknown_fmt;
} wifi_text_t;

// 英文文本
static const wifi_text_t wifi_english_text = {
    .title = "WiFi Settings",
    .enable_wifi = "Enable WiFi",
    .tx_power = "Tx Power",
    .saved_networks = "Saved Networks",
    .available_networks = "Available",
    .refresh_button = "Scan",
    .scan_in_progress = "Scanning...",
    .scan_no_networks = "Search",
    .scan_result_fmt = "%d networks found",
    .details_button = "Details",
    .details_title = "Network Details",
    .status_label = "Status",
    .ssid_label = "SSID",
    .ip_label = "IP",
    .mac_label = "MAC",
    .status_disabled = "Disabled",
    .status_disconnected = "Disconnected",
    .status_connecting = "Connecting...",
    .status_connected = "Connected",
    .keypad_title_fmt = "Password",
    .saved_tag = "Saved",
    .connect_failed_fmt = "Failed to connect: %s",
    .reason_auth_fail = "Authentication failed. Please re-enter the password.",
    .reason_no_ap = "Network not found. It may be out of range or hidden.",
    .reason_assoc_fail = "Association with the access point failed.",
    .reason_handshake_timeout = "Secure handshake timed out.",
    .reason_unknown_fmt = "Unexpected error code %d",
};

// 中文文本
static const wifi_text_t wifi_chinese_text = {
    .title = "无线网络设置",
    .enable_wifi = "启用无线网络",
    .tx_power = "发射功率",
    .saved_networks = "已存网络",
    .available_networks = "可用网络",
    .refresh_button = "扫描",
    .scan_in_progress = "正在扫描...",
    .scan_no_networks = "点击扫描按钮搜索网络",
    .scan_result_fmt = "找到 %d 个网络",
    .details_button = "详细信息",
    .details_title = "网络详情",
    .status_label = "状态",
    .ssid_label = "名称",
    .ip_label = "IP地址",
    .mac_label = "MAC地址",
    .status_disabled = "已禁用",
    .status_disconnected = "已断开",
    .status_connecting = "连接中...",
    .status_connected = "已连接",
    .keypad_title_fmt = "输入 %s 的密码",
    .saved_tag = "已保存",
    .connect_failed_fmt = "连接失败：%s",
    .reason_auth_fail = "认证失败，请重新输入密码。",
    .reason_no_ap = "未找到该网络，可能信号弱或被隐藏。",
    .reason_assoc_fail = "与接入点关联失败。",
    .reason_handshake_timeout = "安全握手过程超时。",
    .reason_unknown_fmt = "未知错误代码 %d",
};

// 获取当前语言文本
static const wifi_text_t* get_wifi_text(void) {
    return (ui_get_current_language() == LANG_CHINESE) ? &wifi_chinese_text : &wifi_english_text;
}

#define UI_WIFI_MAX_SCAN_RESULTS 32
#define UI_WIFI_MAX_SAVED_OPTIONS 32

static const char* TAG = "UI_WIFI";

// UI元素句柄
static lv_obj_t* g_status_label;
static lv_obj_t* g_ssid_label; // 新增SSID标签
static lv_obj_t* g_scan_status_label;
static lv_obj_t* g_available_list;
static lv_obj_t* g_scan_refresh_btn;
static lv_obj_t* g_saved_dropdown;
static lv_obj_t* g_wifi_switch_obj;
static lv_timer_t* g_update_timer;
static bool g_wifi_ui_initialized = false;
static uint32_t g_last_scan_version = UINT32_MAX;
static uint32_t g_last_wifi_list_version = UINT32_MAX;  // 追踪WiFi列表版本
static wifi_manager_state_t g_last_wifi_state = WIFI_STATE_DISABLED;
static int32_t g_last_saved_network_count = -1;
static uint32_t g_last_disconnect_notified_seq = 0;

static void wifi_dropdown_event_cb(lv_event_t* e);
static void wifi_refresh_btn_event_cb(lv_event_t* e);
static void wifi_network_button_event_cb(lv_event_t* e);
static void wifi_network_button_delete_cb(lv_event_t* e);
static void wifi_password_entered_cb(const char* password, void* user_data);
static void wifi_password_keypad_delete_cb(lv_event_t* e);
static void rebuild_available_network_list(void);
static void update_scan_section(void);
static void rebuild_saved_network_dropdown(void);
static void refresh_saved_network_dropdown_if_needed(void);
static void trigger_wifi_scan(bool show_error);
static void wifi_manager_ui_event_cb(void);

typedef struct {
    char ssid[33];
    wifi_auth_mode_t authmode;
    bool is_saved;
} wifi_ap_button_data_t;

typedef struct {
    char ssid[33];
} wifi_connect_request_t;

typedef struct {
    char ssid[33];
    bool from_saved_list;  // 标记是否来自已保存列表
} wifi_delete_request_t;

static const char* get_reason_text(wifi_err_reason_t reason, const wifi_text_t* text) {
    switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        return text->reason_auth_fail;
    case WIFI_REASON_NO_AP_FOUND:
        return text->reason_no_ap;
    case WIFI_REASON_ASSOC_FAIL:
    case WIFI_REASON_ASSOC_EXPIRE:
    case WIFI_REASON_ASSOC_TOOMANY:
        return text->reason_assoc_fail;
    case WIFI_REASON_BEACON_TIMEOUT:
    case WIFI_REASON_CONNECTION_FAIL:
        return text->reason_handshake_timeout;
    default:
        return NULL;
    }
}

/**
 * @brief 更新WiFi信息显示
 */
static void update_wifi_info(void) {
    if (!g_wifi_ui_initialized) {
        return;
    }

    const wifi_text_t* text = get_wifi_text();
    wifi_manager_info_t info = wifi_manager_get_info();
    wifi_manager_state_t previous_state = g_last_wifi_state;
    g_last_wifi_state = info.state;

    switch (info.state) {
    case WIFI_STATE_DISABLED:
        lv_label_set_text_fmt(g_status_label, "%s: %s", text->status_label, text->status_disabled);
        break;
    case WIFI_STATE_DISCONNECTED:
        lv_label_set_text_fmt(g_status_label, "%s: %s", text->status_label,
                              text->status_disconnected);
        break;
    case WIFI_STATE_CONNECTING:
        lv_label_set_text_fmt(g_status_label, "%s: %s", text->status_label,
                              text->status_connecting);
        break;
    case WIFI_STATE_CONNECTED:
        lv_label_set_text_fmt(g_status_label, "%s: %s", text->status_label, text->status_connected);
        break;
    }
    set_language_display(g_status_label);

    if (info.state == WIFI_STATE_CONNECTED) {
        lv_label_set_text_fmt(g_ssid_label, "%s: %s", text->ssid_label, info.ssid);
    } else {
        lv_label_set_text_fmt(g_ssid_label, "%s: N/A", text->ssid_label);
    }
    set_language_display(g_ssid_label);

    if (g_wifi_switch_obj) {
        bool should_be_checked = (info.state != WIFI_STATE_DISABLED);
        bool currently_checked = lv_obj_has_state(g_wifi_switch_obj, LV_STATE_CHECKED);
        if (should_be_checked && !currently_checked) {
            lv_obj_add_state(g_wifi_switch_obj, LV_STATE_CHECKED);
        } else if (!should_be_checked && currently_checked) {
            lv_obj_clear_state(g_wifi_switch_obj, LV_STATE_CHECKED);
        }
    }

    if ((previous_state == WIFI_STATE_CONNECTING || previous_state == WIFI_STATE_CONNECTED) &&
        info.state == WIFI_STATE_DISCONNECTED) {
        uint32_t seq = wifi_manager_get_disconnect_sequence();
        if (seq != 0 && seq != g_last_disconnect_notified_seq) {
            g_last_disconnect_notified_seq = seq;
            wifi_err_reason_t reason = wifi_manager_get_last_disconnect_reason();
            const char* reason_text = get_reason_text(reason, text);
            char reason_desc[128];
            if (reason_text) {
                snprintf(reason_desc, sizeof(reason_desc), "%s (%d)", reason_text, (int)reason);
            } else {
                snprintf(reason_desc, sizeof(reason_desc), text->reason_unknown_fmt, (int)reason);
            }
            char msg[160];
            snprintf(msg, sizeof(msg), text->connect_failed_fmt, reason_desc);
            lv_obj_t* msgbox = lv_msgbox_create(lv_scr_act(), text->title, msg, NULL, true);
            lv_obj_center(msgbox);
            set_language_display(msgbox);
        }
    }

    if (info.state == WIFI_STATE_CONNECTED) {
        g_last_disconnect_notified_seq = wifi_manager_get_disconnect_sequence();
    }
}

static void rebuild_available_network_list(void) {
    if (!g_available_list) {
        return;
    }

    lv_obj_clean(g_available_list);

    wifi_manager_scan_result_t results[UI_WIFI_MAX_SCAN_RESULTS];
    size_t count = wifi_manager_get_scan_results(results, UI_WIFI_MAX_SCAN_RESULTS);
    if (count == 0) {
        return;
    }

    const wifi_text_t* text = get_wifi_text();

    for (size_t i = 0; i < count; i++) {
        wifi_ap_button_data_t* data = lv_mem_alloc(sizeof(wifi_ap_button_data_t));
        if (!data) {
            ESP_LOGW(TAG, "Failed to allocate memory for AP item");
            break;
        }
        memset(data, 0, sizeof(*data));
        strlcpy(data->ssid, results[i].ssid, sizeof(data->ssid));
        data->authmode = results[i].authmode;
        data->is_saved = wifi_manager_get_saved_password(results[i].ssid) != NULL;

        lv_obj_t* item_btn = lv_btn_create(g_available_list);
        lv_obj_set_width(item_btn, LV_PCT(100));
        lv_obj_set_height(item_btn, 52);
        lv_obj_set_flex_flow(item_btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item_btn, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(item_btn, 10, 0);
        lv_obj_set_style_pad_gap(item_btn, 8, 0);
        lv_obj_set_style_bg_opa(item_btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(item_btn, 0, 0);
        theme_apply_to_button(item_btn, false);

        lv_obj_add_event_cb(item_btn, wifi_network_button_event_cb, LV_EVENT_CLICKED, data);
        lv_obj_add_event_cb(item_btn, wifi_network_button_delete_cb, LV_EVENT_DELETE, data);

        lv_obj_t* name_label = lv_label_create(item_btn);
        lv_label_set_text(name_label, results[i].ssid);
        theme_apply_to_label(name_label, false);
        lv_obj_set_flex_grow(name_label, 1);

        char detail_text[64];
        if (data->is_saved) {
            snprintf(detail_text, sizeof(detail_text), "[%s]", text->saved_tag);
        } else {
            detail_text[0] = '\0';
        }

        if (detail_text[0] != '\0') {
            lv_obj_t* detail_label = lv_label_create(item_btn);
            lv_label_set_text(detail_label, detail_text);
            theme_apply_to_label(detail_label, false);
        }
    }
}

static void update_scan_section(void) {
    if (!g_scan_status_label) {
        return;
    }

    const wifi_text_t* text = get_wifi_text();
    wifi_manager_info_t info = wifi_manager_get_info();

    if (info.state == WIFI_STATE_DISABLED) {
        lv_label_set_text(g_scan_status_label, text->status_disabled);
        set_language_display(g_scan_status_label);
        if (g_scan_refresh_btn) {
            lv_obj_add_state(g_scan_refresh_btn, LV_STATE_DISABLED);
        }
        if (g_available_list) {
            lv_obj_add_flag(g_available_list, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clean(g_available_list);
        }
        return;
    }

    bool scanning = wifi_manager_is_scanning();
    if (g_scan_refresh_btn) {
        if (scanning) {
            lv_obj_add_state(g_scan_refresh_btn, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(g_scan_refresh_btn, LV_STATE_DISABLED);
        }
    }
    if (g_available_list) {
        lv_obj_clear_flag(g_available_list, LV_OBJ_FLAG_HIDDEN);
    }

    if (scanning) {
        lv_label_set_text(g_scan_status_label, text->scan_in_progress);
    } else {
        size_t count = wifi_manager_get_scan_result_count();
        if (count == 0) {
            lv_label_set_text(g_scan_status_label, text->scan_no_networks);
        } else {
            lv_label_set_text_fmt(g_scan_status_label, text->scan_result_fmt, (int)count);
        }
    }
    set_language_display(g_scan_status_label);

    uint32_t version = wifi_manager_get_scan_results_version();
    if (version != g_last_scan_version) {
        g_last_scan_version = version;
        rebuild_available_network_list();
    }
}

static void refresh_saved_network_dropdown_if_needed(void) {
    if (!g_saved_dropdown) {
        return;
    }

    // 使用新的列表版本号来检测更新
    uint32_t current_list_version = wifi_manager_get_wifi_list_version();
    int32_t total_count = wifi_manager_get_wifi_list_size();
    
    if (current_list_version != g_last_wifi_list_version || total_count != g_last_saved_network_count) {
        g_last_wifi_list_version = current_list_version;
        rebuild_saved_network_dropdown();
    }
}

static void rebuild_saved_network_dropdown(void) {
    if (!g_saved_dropdown) {
        return;
    }

    int32_t total_count = wifi_manager_get_wifi_list_size();
    g_last_saved_network_count = total_count;

    if (total_count <= 0) {
        lv_dropdown_set_options(g_saved_dropdown, "");
        return;
    }

    int32_t count = total_count;
    if (count > UI_WIFI_MAX_SAVED_OPTIONS) {
        count = UI_WIFI_MAX_SAVED_OPTIONS;
    }

    size_t buffer_len = (size_t)count * 34 + 1;
    char* options = lv_mem_alloc(buffer_len);
    if (!options) {
        ESP_LOGW(TAG, "Failed to allocate dropdown buffer");
        return;
    }
    options[0] = '\0';

    for (int32_t i = 0; i < count; i++) {
        const char* ssid = wifi_manager_get_wifi_ssid_by_index(i);
        if (!ssid) {
            continue;
        }
        if (i > 0) {
            strlcat(options, "\n", buffer_len);
        }
        strlcat(options, ssid, buffer_len);
    }

    lv_dropdown_set_options(g_saved_dropdown, options);
    lv_mem_free(options);
    
    ESP_LOGI(TAG, "Saved network dropdown rebuilt with %d entries", count);
}

static void trigger_wifi_scan(bool show_error) {
    if (wifi_manager_is_scanning()) {
        return;
    }

    g_last_scan_version = UINT32_MAX;
    esp_err_t err = wifi_manager_start_scan(false);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi scan start failed: %s", esp_err_to_name(err));
        if (show_error) {
            char msg[96];
            snprintf(msg, sizeof(msg), "Scan failed: %s", esp_err_to_name(err));
            lv_obj_t* msgbox = lv_msgbox_create(lv_scr_act(), get_wifi_text()->title, msg, NULL, true);
            lv_obj_center(msgbox);
            set_language_display(msgbox);
        }
        return;
    }

    update_scan_section();
}

static void wifi_manager_ui_event_cb(void) {
    g_last_scan_version = UINT32_MAX;
    g_last_wifi_list_version = UINT32_MAX;  // 重置列表版本号
    g_last_saved_network_count = -1;
    g_last_wifi_state = wifi_manager_get_info().state;
    g_last_disconnect_notified_seq = wifi_manager_get_disconnect_sequence();
}

static void wifi_refresh_btn_event_cb(lv_event_t* e) {
    (void)e;
    trigger_wifi_scan(true);
}

static void wifi_network_button_delete_cb(lv_event_t* e) {
    wifi_ap_button_data_t* data = lv_event_get_user_data(e);
    if (data) {
        lv_mem_free(data);
    }
}

static void wifi_network_button_event_cb(lv_event_t* e) {
    wifi_ap_button_data_t* data = lv_event_get_user_data(e);
    if (!data) {
        return;
    }

    if (data->authmode == WIFI_AUTH_OPEN) {
        wifi_manager_connect_with_password(data->ssid, "");
        return;
    }

    const wifi_text_t* text = get_wifi_text();
    const char* saved_password = wifi_manager_get_saved_password(data->ssid);

    wifi_connect_request_t* request = lv_mem_alloc(sizeof(wifi_connect_request_t));
    if (!request) {
        ESP_LOGW(TAG, "Failed to allocate connect request");
        return;
    }
    memset(request, 0, sizeof(*request));
    strlcpy(request->ssid, data->ssid, sizeof(request->ssid));

    char title[96];
    snprintf(title, sizeof(title), text->keypad_title_fmt, data->ssid);

    lv_obj_t* keypad = ui_numeric_keypad_create(lv_scr_act(), title, saved_password,
                                                wifi_password_entered_cb, request);
    if (!keypad) {
        lv_mem_free(request);
        return;
    }

    lv_obj_add_event_cb(keypad, wifi_password_keypad_delete_cb, LV_EVENT_DELETE, request);
}

static void wifi_password_entered_cb(const char* password, void* user_data) {
    wifi_connect_request_t* request = (wifi_connect_request_t*)user_data;
    if (!request) {
        return;
    }
    wifi_manager_connect_with_password(request->ssid, password);
}

static void wifi_password_keypad_delete_cb(lv_event_t* e) {
    wifi_connect_request_t* request = lv_event_get_user_data(e);
    if (request) {
        lv_mem_free(request);
    }
}

/**
 * @brief 确认删除WiFi的回调
 */
static void confirm_delete_wifi_cb(lv_event_t* e) {
    lv_obj_t* btn = lv_event_get_target(e);
    wifi_delete_request_t* request = (wifi_delete_request_t*)lv_event_get_user_data(e);
    
    if (!request) {
        return;
    }
    
    esp_err_t err = wifi_manager_remove_wifi_from_list(request->ssid);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WiFi removed: %s", request->ssid);
        // 触发列表更新
        g_last_wifi_list_version = UINT32_MAX;
    } else {
        ESP_LOGW(TAG, "Failed to remove WiFi: %s", request->ssid);
    }
    
    lv_mem_free(request);
}

/**
 * @brief 显示删除WiFi的确认对话框
 */
static void show_delete_wifi_confirm(const char* ssid) {
    if (!ssid || ssid[0] == '\0') {
        return;
    }
    
    const wifi_text_t* text = get_wifi_text();
    
    char msg[128];
    snprintf(msg, sizeof(msg), "Delete WiFi network '%s'?", ssid);
    
    lv_obj_t* buttons[] = {
        lv_btn_create(lv_layer_top()),
        lv_btn_create(lv_layer_top()),
        NULL
    };
    
    lv_obj_t* msgbox = lv_msgbox_create(lv_scr_act(), "Delete Network", msg, 
                                        (const char*[]){"Delete", "Cancel", ""}, false);
    
    // 为删除按钮设置回调
    lv_obj_t* delete_btn = lv_msgbox_get_btns(msgbox);
    if (delete_btn) {
        wifi_delete_request_t* request = lv_mem_alloc(sizeof(wifi_delete_request_t));
        if (request) {
            strlcpy(request->ssid, ssid, sizeof(request->ssid));
            request->from_saved_list = true;
            lv_obj_add_event_cb(delete_btn, confirm_delete_wifi_cb, LV_EVENT_CLICKED, request);
        }
    }
    
    lv_obj_center(msgbox);
    set_language_display(msgbox);
}

/**
 * @brief UI更新定时器的回调
 * @param timer
 */
static void ui_update_timer_cb(lv_timer_t* timer) {
    (void)timer;
    if (!g_wifi_ui_initialized) {
        return;
    }

    update_wifi_info();
    update_scan_section();
    refresh_saved_network_dropdown_if_needed();
}

/**
 * @brief 页面清理回调
 * @param e
 */
static void ui_wifi_settings_cleanup(lv_event_t* e) {
    if (g_update_timer) {
        lv_timer_del(g_update_timer);
        g_update_timer = NULL;
    }
    wifi_manager_register_event_callback(NULL);
    g_scan_status_label = NULL;
    g_available_list = NULL;
    g_scan_refresh_btn = NULL;
    g_saved_dropdown = NULL;
    g_wifi_switch_obj = NULL;
    g_last_scan_version = UINT32_MAX;
    g_last_wifi_list_version = UINT32_MAX;  // 重置列表版本号
    g_last_saved_network_count = -1;
    g_last_wifi_state = WIFI_STATE_DISABLED;
    g_last_disconnect_notified_seq = 0;
    g_wifi_ui_initialized = false;
    // The parent's children are cleaned by LVGL automatically.
}

/**
 * @brief “详细信息”按钮回调
 * @param e
 */
static void details_btn_event_cb(lv_event_t* e) {
    lv_obj_t* screen = lv_scr_act();
    wifi_manager_info_t info = wifi_manager_get_info();
    const wifi_text_t* text = get_wifi_text();

    char msg_buffer[200];
    char mac_str[24];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X", info.mac_addr[0],
             info.mac_addr[1], info.mac_addr[2], info.mac_addr[3], info.mac_addr[4],
             info.mac_addr[5]);

    const char* status_str;
    switch (info.state) {
    case WIFI_STATE_DISABLED:
        status_str = text->status_disabled;
        break;
    case WIFI_STATE_DISCONNECTED:
        status_str = text->status_disconnected;
        break;
    case WIFI_STATE_CONNECTING:
        status_str = text->status_connecting;
        break;
    case WIFI_STATE_CONNECTED:
        status_str = text->status_connected;
        break;
    default:
        status_str = "Unknown";
        break;
    }

    snprintf(msg_buffer, sizeof(msg_buffer),
             "%s: %s\n"
             "%s: %s\n"
             "%s: %s\n"
             "%s: %s",
             text->status_label, status_str, text->ssid_label,
             info.state == WIFI_STATE_CONNECTED ? (char*)info.ssid : "N/A", text->ip_label,
             info.ip_addr, text->mac_label, mac_str);
    lv_obj_t* msgbox = lv_msgbox_create(screen, text->details_title, msg_buffer, NULL, true);
    lv_obj_center(msgbox);
    set_language_display(msgbox);
}

/**
 * @brief WiFi开关的事件回调
 * @param e 事件对象
 */
static void wifi_switch_event_cb(lv_event_t* e) {
    lv_obj_t* switcher = lv_event_get_target(e);
    wifi_manager_info_t info = wifi_manager_get_info();

    if (lv_obj_has_state(switcher, LV_STATE_CHECKED)) {
        if (info.state == WIFI_STATE_DISABLED) {
            esp_err_t err = wifi_manager_start();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(err));
                lv_obj_clear_state(switcher, LV_STATE_CHECKED);
                return;
            }
        }
        trigger_wifi_scan(false);
    } else {
        wifi_manager_stop();
        if (g_available_list) {
            lv_obj_clean(g_available_list);
        }
        g_last_scan_version = UINT32_MAX;
        update_scan_section();
    }
    update_wifi_info();
}

/**
 * @brief WiFi功率滑块的事件回调
 * @param e 事件对象
 */
static void power_slider_event_cb(lv_event_t* e) {
    lv_obj_t* slider = lv_event_get_target(e);
    lv_obj_t* power_label = (lv_obj_t*)lv_event_get_user_data(e);
    int32_t power_dbm = lv_slider_get_value(slider);
    const wifi_text_t* text = get_wifi_text(); // 获取当前语言文本

    lv_label_set_text_fmt(power_label, "%s: %d dBm", text->tx_power, (int)power_dbm);
    set_language_display(power_label);
    wifi_manager_set_power((int8_t)power_dbm);
}

/**
 * @brief 创建WiFi设置界面的函数
 * @param parent 父对象, 通常是 lv_scr_act()
 */
void ui_wifi_settings_create(lv_obj_t* parent) {
    g_wifi_ui_initialized = true;
    const wifi_text_t* text = get_wifi_text();

    theme_apply_to_screen(parent);

    wifi_manager_register_event_callback(wifi_manager_ui_event_cb);

    // 1. 创建页面父级容器
    lv_obj_t* page_parent_container;
    ui_create_page_parent_container(parent, &page_parent_container);
    lv_obj_add_event_cb(page_parent_container, ui_wifi_settings_cleanup, LV_EVENT_DELETE, NULL);

    // 2. 创建顶部栏
    lv_obj_t* top_bar_container;
    lv_obj_t* title_container;
    ui_create_top_bar(page_parent_container, text->title, false, &top_bar_container,
                      &title_container, NULL);

    // 3. 创建页面内容容器
    lv_obj_t* content_container;
    ui_create_page_content_area(page_parent_container, &content_container);

    // 设置内容容器为垂直布局
    lv_obj_set_flex_flow(content_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(content_container, 5, 0);
    lv_obj_set_style_pad_gap(content_container, 10, 0);
    // 隐藏滚动条，参考主页代码
    lv_obj_set_style_width(content_container, 0, LV_PART_SCROLLBAR);
    lv_obj_set_style_opa(content_container, LV_OPA_0, LV_PART_SCROLLBAR);

    // === 创建WiFi功能容器 ===
    lv_obj_t* wifi_container = lv_obj_create(content_container);
    lv_obj_set_width(wifi_container, lv_pct(100));
    lv_obj_set_height(wifi_container, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wifi_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(wifi_container, 10, 0);
    lv_obj_set_style_pad_gap(wifi_container, 8, 0);
    // 隐藏滚动条
    lv_obj_set_style_width(wifi_container, 0, LV_PART_SCROLLBAR);
    lv_obj_set_style_opa(wifi_container, LV_OPA_0, LV_PART_SCROLLBAR);
    theme_apply_to_container(wifi_container);

    // WiFi容器标题
    lv_obj_t* wifi_title = lv_label_create(wifi_container);
    lv_label_set_text_fmt(wifi_title, "%s %s", LV_SYMBOL_WIFI, text->title);
    theme_apply_to_label(wifi_title, false);
    lv_obj_set_style_text_color(wifi_title, lv_palette_main(LV_PALETTE_BLUE), 0);
    set_language_display(wifi_title);

    // --- 1. WiFi开关 ---
    lv_obj_t* switch_item = lv_obj_create(wifi_container);
    lv_obj_set_width(switch_item, lv_pct(100));
    lv_obj_set_height(switch_item, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(switch_item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(switch_item, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(switch_item, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(switch_item, 0, 0);
    // 隐藏滚动条
    lv_obj_clear_flag(switch_item, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* switch_label = lv_label_create(switch_item);
    lv_label_set_text(switch_label, text->enable_wifi);
    theme_apply_to_label(switch_label, false);
    set_language_display(switch_label);

    lv_obj_t* wifi_switch = lv_switch_create(switch_item);
    theme_apply_to_switch(wifi_switch);
    lv_obj_add_event_cb(wifi_switch, wifi_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    g_wifi_switch_obj = wifi_switch;

    // --- 2. WiFi功率控制 ---
    lv_obj_t* slider_container_item = lv_obj_create(wifi_container);
    lv_obj_set_width(slider_container_item, lv_pct(100));
    lv_obj_set_height(slider_container_item, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(slider_container_item, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(slider_container_item, 5, 0);
    lv_obj_set_style_bg_opa(slider_container_item, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(slider_container_item, 0, 0);
    // 禁止滚动
    lv_obj_clear_flag(slider_container_item, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* power_val_label = lv_label_create(slider_container_item);
    theme_apply_to_label(power_val_label, false);

    lv_obj_t* power_slider = lv_slider_create(slider_container_item);
    lv_obj_set_size(power_slider, lv_pct(100), 8);
    lv_obj_set_style_pad_all(power_slider, 2, LV_PART_KNOB);
    lv_slider_set_range(power_slider, 2, 20); // ESP32功率范围
    lv_obj_add_event_cb(power_slider, power_slider_event_cb, LV_EVENT_VALUE_CHANGED,
                        power_val_label);

    // --- 3. 扫描与可用网络 ---
    lv_obj_t* scan_header = lv_obj_create(wifi_container);
    lv_obj_set_width(scan_header, lv_pct(100));
    lv_obj_set_height(scan_header, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(scan_header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(scan_header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(scan_header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scan_header, 0, 0);
    lv_obj_clear_flag(scan_header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* available_label = lv_label_create(scan_header);
    lv_label_set_text(available_label, text->available_networks);
    theme_apply_to_label(available_label, false);
    set_language_display(available_label);

    g_scan_refresh_btn = lv_btn_create(scan_header);
    theme_apply_to_button(g_scan_refresh_btn, false);
    lv_obj_set_style_pad_all(g_scan_refresh_btn, 6, 0);
    lv_obj_add_event_cb(g_scan_refresh_btn, wifi_refresh_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* refresh_label = lv_label_create(g_scan_refresh_btn);
    lv_label_set_text_fmt(refresh_label, "%s", text->refresh_button);
    theme_apply_to_label(refresh_label, false);
    set_language_display(refresh_label);

    g_scan_status_label = lv_label_create(wifi_container);
    lv_label_set_text(g_scan_status_label, text->scan_no_networks);
    theme_apply_to_label(g_scan_status_label, false);
    set_language_display(g_scan_status_label);

    g_available_list = lv_obj_create(wifi_container);
    lv_obj_set_width(g_available_list, lv_pct(100));
    lv_obj_set_height(g_available_list, 200);
    lv_obj_set_flex_flow(g_available_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(g_available_list, 4, 0);
    lv_obj_set_style_pad_gap(g_available_list, 6, 0);
    lv_obj_set_style_bg_opa(g_available_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_available_list, 0, 0);
    lv_obj_set_scroll_dir(g_available_list, LV_DIR_VER);
    lv_obj_set_style_width(g_available_list, 0, LV_PART_SCROLLBAR);
    lv_obj_set_style_opa(g_available_list, LV_OPA_0, LV_PART_SCROLLBAR);
    theme_apply_to_container(g_available_list);

    // --- 4. 已保存的网络列表 ---
    lv_obj_t* dropdown_container_item = lv_obj_create(wifi_container);
    lv_obj_set_width(dropdown_container_item, lv_pct(100));
    lv_obj_set_height(dropdown_container_item, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(dropdown_container_item, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(dropdown_container_item, 5, 0);
    lv_obj_set_style_bg_opa(dropdown_container_item, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dropdown_container_item, 0, 0);
    lv_obj_clear_flag(dropdown_container_item, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* dropdown_title_label = lv_label_create(dropdown_container_item);
    lv_label_set_text(dropdown_title_label, text->saved_networks);
    theme_apply_to_label(dropdown_title_label, false);
    set_language_display(dropdown_title_label);

    g_saved_dropdown = lv_dropdown_create(dropdown_container_item);
    lv_obj_set_width(g_saved_dropdown, lv_pct(100));
    theme_apply_to_button(g_saved_dropdown, false);
    lv_obj_add_event_cb(g_saved_dropdown, wifi_dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    g_last_saved_network_count = -1;
    g_last_wifi_list_version = UINT32_MAX;  // 初始化列表版本号
    rebuild_saved_network_dropdown();

    // --- 4. WiFi信息显示 ---
    lv_obj_t* info_container_item = lv_obj_create(wifi_container);
    lv_obj_set_width(info_container_item, lv_pct(100));
    lv_obj_set_height(info_container_item, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(info_container_item, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(info_container_item, 5, 0);
    lv_obj_set_style_bg_opa(info_container_item, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(info_container_item, 0, 0);
    // 隐藏滚动条
    lv_obj_set_style_width(info_container_item, 0, LV_PART_SCROLLBAR);
    lv_obj_set_style_opa(info_container_item, LV_OPA_0, LV_PART_SCROLLBAR);
    lv_obj_clear_flag(info_container_item, LV_OBJ_FLAG_SCROLLABLE);

    g_status_label = lv_label_create(info_container_item);
    theme_apply_to_label(g_status_label, false);
    g_ssid_label = lv_label_create(info_container_item);
    theme_apply_to_label(g_ssid_label, false);

    // --- 5. "详细信息"按钮 ---
    lv_obj_t* details_btn = lv_btn_create(wifi_container);
    lv_obj_set_width(details_btn, lv_pct(100));
    lv_obj_set_height(details_btn, 40);
    theme_apply_to_button(details_btn, false);
    lv_obj_add_event_cb(details_btn, details_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* details_btn_label = lv_label_create(details_btn);
    lv_label_set_text_fmt(details_btn_label, "%s", text->details_button);
    lv_obj_center(details_btn_label);
    theme_apply_to_label(details_btn_label, false);
    set_language_display(details_btn_label);

    // 初始化UI状态
    wifi_manager_info_t current_info = wifi_manager_get_info();
    if (current_info.state == WIFI_STATE_DISABLED) {
        lv_obj_clear_state(wifi_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_add_state(wifi_switch, LV_STATE_CHECKED);
    }

    int8_t power_dbm = 20; // Default power
    wifi_manager_get_power(&power_dbm);
    lv_slider_set_value(power_slider, power_dbm, LV_ANIM_OFF);
    lv_label_set_text_fmt(power_val_label, "%s: %d dBm", text->tx_power, power_dbm);
    set_language_display(power_val_label);

    g_last_wifi_state = current_info.state;
    g_last_disconnect_notified_seq = wifi_manager_get_disconnect_sequence();
    g_last_scan_version = UINT32_MAX;
    g_last_wifi_list_version = UINT32_MAX;  // 初始化列表版本号
    update_scan_section();
    if (current_info.state != WIFI_STATE_DISABLED) {
        trigger_wifi_scan(false);
    }

    // 创建并启动UI更新定时器
    g_update_timer = lv_timer_create(ui_update_timer_cb, 500, NULL);

    // 立即更新一次UI
    update_wifi_info();
}

static void wifi_dropdown_event_cb(lv_event_t* e) {
    lv_obj_t* dropdown = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_VALUE_CHANGED) {
        if (lv_dropdown_get_option_cnt(dropdown) == 0) {
            return;
        }
        uint16_t selected_index = lv_dropdown_get_selected(dropdown);
        wifi_manager_connect_to_index(selected_index);
    }
}
