#include "esp_log.h" // 引入日志功能
#include "ui.h"
#include "wifi_manager.h" // 引入WiFi管理器头文件
#include <stdio.h>        // 为了使用 snprintf
#include <string.h>       // 为了使用 strcat 等字符串函数

// 为了实现返回功能，需要前向声明设置菜单的创建函数
void ui_settings_create(lv_obj_t* parent);

// 语言文本定义
typedef struct {
    const char* title;
    const char* enable_wifi;
    const char* tx_power;
    const char* saved_networks;
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
} wifi_text_t;

// 英文文本
static const wifi_text_t wifi_english_text = {
    .title = "WiFi Settings",
    .enable_wifi = "Enable WiFi",
    .tx_power = "Tx Power",
    .saved_networks = "Saved Networks",
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
};

// 中文文本
static const wifi_text_t wifi_chinese_text = {
    .title = "无线网络设置",
    .enable_wifi = "启用无线网络",
    .tx_power = "发射功率",
    .saved_networks = "已存网络",
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
};

// 获取当前语言文本
static const wifi_text_t* get_wifi_text(void) {
    return (ui_get_current_language() == LANG_CHINESE) ? &wifi_chinese_text : &wifi_english_text;
}

// UI元素句柄
static lv_obj_t* g_status_label;
static lv_obj_t* g_ssid_label; // 新增SSID标签
static lv_timer_t* g_update_timer;
static bool g_wifi_ui_initialized = false;

static void wifi_dropdown_event_cb(lv_event_t* e);

/**
 * @brief 更新WiFi信息显示
 */
static void update_wifi_info(void) {
    if (!g_wifi_ui_initialized) {
        return;
    }

    const wifi_text_t* text = get_wifi_text();
    wifi_manager_info_t info = wifi_manager_get_info();

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
}

/**
 * @brief UI更新定时器的回调
 * @param timer
 */
static void ui_update_timer_cb(lv_timer_t* timer) {
    if (g_wifi_ui_initialized) {
        update_wifi_info();
    }
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

    if (lv_obj_has_state(switcher, LV_STATE_CHECKED)) {
        wifi_manager_start();
    } else {
        wifi_manager_stop();
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
    lv_label_set_text_fmt(wifi_title, "%s %s", LV_SYMBOL_WIFI, "WiFi Settings");
    theme_apply_to_label(wifi_title, false);
    lv_obj_set_style_text_color(wifi_title, lv_palette_main(LV_PALETTE_BLUE), 0);

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

    // --- 3. WiFi连接列表 ---
    lv_obj_t* dropdown_container_item = lv_obj_create(wifi_container);
    lv_obj_set_width(dropdown_container_item, lv_pct(100));
    lv_obj_set_height(dropdown_container_item, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(dropdown_container_item, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(dropdown_container_item, 5, 0);
    lv_obj_set_style_bg_opa(dropdown_container_item, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dropdown_container_item, 0, 0);
    // 禁止滚动
    lv_obj_clear_flag(dropdown_container_item, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* dropdown_title_label = lv_label_create(dropdown_container_item);
    lv_label_set_text(dropdown_title_label, text->saved_networks);
    theme_apply_to_label(dropdown_title_label, false);
    set_language_display(dropdown_title_label);

    lv_obj_t* wifi_dropdown = lv_dropdown_create(dropdown_container_item);
    lv_obj_set_width(wifi_dropdown, lv_pct(100));
    theme_apply_to_button(wifi_dropdown, false);
    int32_t wifi_count = wifi_manager_get_wifi_list_size();
    if (wifi_count > 0) {
        char ssid_list[512] = {0};
        for (int i = 0; i < wifi_count; i++) {
            const char* ssid = wifi_manager_get_wifi_ssid_by_index(i);
            if (ssid) {
                strcat(ssid_list, ssid);
                if (i < wifi_count - 1) {
                    strcat(ssid_list, "\n");
                }
            }
        }
        lv_dropdown_set_options(wifi_dropdown, ssid_list);
    }
    lv_obj_add_event_cb(wifi_dropdown, wifi_dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

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

    // 创建并启动UI更新定时器
    g_update_timer = lv_timer_create(ui_update_timer_cb, 500, NULL);

    // 立即更新一次UI
    update_wifi_info();
}

static void wifi_dropdown_event_cb(lv_event_t* e) {
    lv_obj_t* dropdown = lv_event_get_target(e);
    uint16_t selected_index = lv_dropdown_get_selected(dropdown);
    wifi_manager_connect_to_index(selected_index);
}
