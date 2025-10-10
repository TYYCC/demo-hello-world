/**
 * @file ui_settings.c
 * @brief 设置界面实现
 * @author TidyCraze
 * @date 2025-08-14
 */
#include "esp_log.h"
#include "my_font.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "settings_manager.h" // For transfer mode settings
#include "st7789.h"           // For backlight control
#include "theme_manager.h"
#include "ui.h"


static const char* TAG = "UI_SETTINGS";

// 语言文本定义
typedef struct {
    const char* settings_title;
    const char* language_label;
    const char* theme_label;
    const char* about_label;
    const char* back_button;
    const char* english_text;
    const char* chinese_text;
    const char* light_theme;
    const char* dark_theme;
    const char* transfer_mode;
    const char* version_info;
    const char* language_changed;
    const char* wifi_settings_label;
    const char* backlight_label;
    const char* auto_pairing_label;
} ui_text_t;

// 英文文本
static const ui_text_t english_text = {.settings_title = "SETTINGS",
                                       .language_label = "Language:",
                                       .theme_label = "Theme:",
                                       .about_label = "About:",
                                       .back_button = "Back",
                                       .english_text = "English",
                                       .chinese_text = "Chinese",
                                       .light_theme = "Light",
                                       .dark_theme = "Dark",
                                       .transfer_mode = "Transfer Mode",
                                       .version_info = "ESP32-S3 Demo v1.0.0",
                                       .language_changed = "Language Changed!",
                                       .wifi_settings_label = "WiFi Settings",
                                       .backlight_label = "Backlight:",
                                       .auto_pairing_label = "Auto Pairing"};

// 中文文本（需要中文字体支持）
static const ui_text_t chinese_text = {.settings_title = "设置",
                                       .language_label = "语言:",
                                       .theme_label = "主题:",
                                       .about_label = "关于本机",
                                       .back_button = "返回",
                                       .english_text = "英文",
                                       .chinese_text = "中文",
                                       .light_theme = "浅色",
                                       .dark_theme = "深色",
                                       .transfer_mode = "图传模式",
                                       .version_info = "ESP32-S3 演示 v1.0.0",
                                       .language_changed = "语言已切换!",
                                       .wifi_settings_label = "无线网络设置",
                                       .backlight_label = "背光:",
                                       .auto_pairing_label = "自动配对"};

// 获取当前语言文本
static const ui_text_t* get_current_text(void) {
    return (g_current_language == LANG_CHINESE) ? &chinese_text : &english_text;
}

// NVS保存语言设置
static void save_language_setting(ui_language_t lang) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("ui_settings", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_set_u8(nvs_handle, "language", (uint8_t)lang);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "Language setting saved: %s", lang == LANG_CHINESE ? "Chinese" : "English");
    }
}

// NVS读取语言设置
ui_language_t load_language_setting(void) {
    nvs_handle_t nvs_handle;
    uint8_t lang = LANG_ENGLISH;
    esp_err_t err = nvs_open("ui_settings", NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        err = nvs_get_u8(nvs_handle, "language", &lang);
        nvs_close(nvs_handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Language setting loaded: %s",
                     lang == LANG_CHINESE ? "Chinese" : "English");
        }
    }
    return (ui_language_t)lang;
}

// 语言切换回调
static void language_switch_cb(lv_event_t* e) {
    lv_obj_t* sw = lv_event_get_target(e);
    bool is_chinese = lv_obj_has_state(sw, LV_STATE_CHECKED);

    g_current_language = is_chinese ? LANG_CHINESE : LANG_ENGLISH;
    save_language_setting(g_current_language);

    // 显示切换提示
    lv_obj_t* screen = lv_scr_act();
    lv_obj_t* msgbox =
        lv_msgbox_create(screen, "Info", get_current_text()->language_changed, NULL, true);
    lv_obj_center(msgbox);
    lv_obj_set_style_text_font(msgbox, get_current_font(), 0);

    ESP_LOGI(TAG, "Language switched to: %s", is_chinese ? "Chinese" : "English");
}

// WiFi设置按钮回调
static void wifi_settings_btn_cb(lv_event_t* e) {
    lv_obj_t* screen = lv_scr_act();
    if (screen) {
        lv_obj_clean(screen);
        ui_wifi_settings_create(screen); // 跳转到WiFi设置页面
    }
}

// 自动配对按钮回调
static void auto_pairing_btn_cb(lv_event_t* e) {
    // 启动自动配对功能
    extern void auto_pairing_start(void);
    auto_pairing_start();
}

// 关于信息回调
static void about_btn_cb(lv_event_t* e) {
    lv_obj_t* screen = lv_scr_act();
    const char* en_about_msg =  "ESP32-S3 System\n\n"
                                "Version: v2.62\n"
                                "Hardware: ESP32-S3-N16R8\n"
                                "Display: IPS 240x320";

    const char* cn_about_msg =  "ESP32-S3 系统\n\n"
                                "版本: v2.62\n"
                                "硬件: ESP32-S3-N16R8\n"
                                "显示屏: IPS 240x320";

    const char* about_msg = (g_current_language == LANG_CHINESE) ? cn_about_msg : en_about_msg;

    lv_obj_t* msgbox = lv_msgbox_create(screen, "About", about_msg, NULL, true);
    set_language_display(msgbox);
    lv_obj_set_size(msgbox, 260, 200);
    lv_obj_center(msgbox);
}

// 主题下拉列表回调
static void theme_dropdown_cb(lv_event_t* e) {
    lv_obj_t* dropdown = lv_event_get_target(e);
    uint16_t selected = lv_dropdown_get_selected(dropdown);

    // 根据选择的索引确定主题类型
    theme_type_t theme_type;
    switch (selected) {
    case 0:
        theme_type = THEME_MORANDI;
        break;
    case 1:
        theme_type = THEME_DARK;
        break;
    case 2:
        theme_type = THEME_LIGHT;
        break;
    case 3:
        theme_type = THEME_BLUE;
        break;
    case 4:
        theme_type = THEME_GREEN;
        break;
    default:
        theme_type = THEME_MORANDI;
        break;
    }

    // 设置新主题
    theme_set_current(theme_type);

    // 应用新主题到当前屏幕
    lv_obj_t* screen = lv_scr_act();
    theme_apply_to_screen(screen);

    // 显示切换提示
    const theme_t* theme = theme_get_current_theme();
    lv_obj_t* msgbox = lv_msgbox_create(screen, "Theme Changed", theme->name, NULL, true);
    lv_obj_center(msgbox);

    ESP_LOGI(TAG, "Theme switched to: %s", theme->name);
}

// 背光滑块回调
static void backlight_slider_cb(lv_event_t* e) {
    lv_obj_t* slider = lv_event_get_target(e);
    lv_obj_t* label = (lv_obj_t*)lv_event_get_user_data(e);

    int32_t brightness = lv_slider_get_value(slider);
    lv_label_set_text_fmt(label, "%ld%%", brightness);

    // Set and save backlight
    st7789_set_backlight((uint8_t)brightness);
    settings_set_backlight((uint8_t)brightness);
}

// Callbacks for the new transfer mode checkboxes
static void transfer_mode_tcp_cb(lv_event_t* e);
static void transfer_mode_udp_cb(lv_event_t* e);

// 创建设置界面
void ui_settings_create(lv_obj_t* parent) {
    // 加载保存的语言设置
    g_current_language = load_language_setting();
    const ui_text_t* text = get_current_text();

    // 应用当前主题到屏幕
    theme_apply_to_screen(parent);

    // 创建页面父级容器（统一管理整个页面）
    lv_obj_t* page_parent_container;
    ui_create_page_parent_container(parent, &page_parent_container);

    // 创建顶部栏容器（包含返回按钮和标题, 但不包含设置按钮）
    lv_obj_t* top_bar_container;
    lv_obj_t* title_container;
    ui_create_top_bar(page_parent_container, text->settings_title, false, &top_bar_container,
                      &title_container, NULL);

    // 创建页面内容容器
    lv_obj_t* content_container;
    ui_create_page_content_area(page_parent_container, &content_container);

    // 设置内容容器为可滚动列表布局
    lv_obj_set_flex_flow(content_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(content_container, 10, 0);
    lv_obj_set_style_pad_gap(content_container, 25, 0);

    // --- 语言设置 ---
    lv_obj_t* lang_group = lv_obj_create(content_container);
    lv_obj_set_width(lang_group, lv_pct(100));
    lv_obj_set_height(lang_group, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(lang_group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(lang_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lang_group, 0, 0);
    lv_obj_set_style_pad_all(lang_group, 0, 0);
    lv_obj_set_style_pad_gap(lang_group, 5, 0);

    lv_obj_t* lang_row = lv_obj_create(lang_group);
    lv_obj_set_width(lang_row, lv_pct(100));
    lv_obj_set_height(lang_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(lang_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(lang_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(lang_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lang_row, 0, 0);
    lv_obj_set_style_pad_all(lang_row, 0, 0);

    lv_obj_t* lang_label = lv_label_create(lang_row);
    lv_label_set_text(lang_label, text->language_label);
    theme_apply_to_label(lang_label, false);
    set_language_display(lang_label);

    lv_obj_t* lang_switch = lv_switch_create(lang_row);
    theme_apply_to_switch(lang_switch);
    if (g_current_language == LANG_CHINESE) {
        lv_obj_add_state(lang_switch, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(lang_switch, language_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t* lang_status = lv_label_create(lang_group);
    lv_label_set_text(lang_status,
                      g_current_language == LANG_CHINESE ? text->chinese_text : text->english_text);
    theme_apply_to_label(lang_status, false);
    set_language_display(lang_status);

    // --- 背光设置 ---
    lv_obj_t* backlight_group = lv_obj_create(content_container);
    lv_obj_set_width(backlight_group, lv_pct(100));
    lv_obj_set_height(backlight_group, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(backlight_group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(backlight_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(backlight_group, 0, 0);
    lv_obj_set_style_pad_all(backlight_group, 0, 0);
    lv_obj_set_style_pad_gap(backlight_group, 10, 0);

    lv_obj_t* backlight_row = lv_obj_create(backlight_group);
    lv_obj_set_width(backlight_row, lv_pct(100));
    lv_obj_set_height(backlight_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(backlight_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(backlight_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(backlight_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(backlight_row, 0, 0);
    lv_obj_set_style_pad_all(backlight_row, 0, 0);

    lv_obj_t* backlight_label = lv_label_create(backlight_row);
    lv_label_set_text(backlight_label, text->backlight_label);
    theme_apply_to_label(backlight_label, false);
    set_language_display(backlight_label);

    lv_obj_t* backlight_value_label = lv_label_create(backlight_row);
    uint8_t current_backlight = settings_get_backlight();
    lv_label_set_text_fmt(backlight_value_label, "%u%%", current_backlight);
    theme_apply_to_label(backlight_value_label, false);

    lv_obj_t* backlight_slider = lv_slider_create(backlight_group);
    lv_obj_set_width(backlight_slider, lv_pct(100));
    lv_slider_set_range(backlight_slider, 10, 100);
    lv_slider_set_value(backlight_slider, current_backlight, LV_ANIM_OFF);
    lv_obj_add_event_cb(backlight_slider, backlight_slider_cb, LV_EVENT_VALUE_CHANGED,
                        backlight_value_label);

    // --- 主题设置 ---
    lv_obj_t* theme_group = lv_obj_create(content_container);
    lv_obj_set_width(theme_group, lv_pct(100));
    lv_obj_set_height(theme_group, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(theme_group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(theme_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(theme_group, 0, 0);
    lv_obj_set_style_pad_all(theme_group, 0, 0);
    lv_obj_set_style_pad_gap(theme_group, 10, 0);

    lv_obj_t* theme_label = lv_label_create(theme_group);
    lv_label_set_text(theme_label, text->theme_label);
    theme_apply_to_label(theme_label, false);
    set_language_display(theme_label);

    lv_obj_t* theme_dropdown = lv_dropdown_create(theme_group);
    lv_obj_set_width(theme_dropdown, lv_pct(100));
    lv_dropdown_set_options(theme_dropdown, "Morandi\nDark\nLight\nBlue\nGreen");
    lv_obj_add_event_cb(theme_dropdown, theme_dropdown_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // 设置下拉列表样式
    lv_obj_set_style_radius(theme_dropdown, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(theme_dropdown, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(
        theme_dropdown, theme_get_color(theme_get_current_theme()->colors.border), LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        theme_dropdown, theme_get_color(theme_get_current_theme()->colors.surface), LV_PART_MAIN);
    lv_obj_set_style_text_color(theme_dropdown,
                                theme_get_color(theme_get_current_theme()->colors.text_primary),
                                LV_PART_MAIN);
    lv_obj_set_style_text_font(theme_dropdown, &lv_font_montserrat_14, LV_PART_MAIN);

    theme_type_t current_theme_type = theme_get_current();
    int selected_index = 0;
    switch (current_theme_type) {
    case THEME_MORANDI:
        selected_index = 0;
        break;
    case THEME_DARK:
        selected_index = 1;
        break;
    case THEME_LIGHT:
        selected_index = 2;
        break;
    case THEME_BLUE:
        selected_index = 3;
        break;
    case THEME_GREEN:
        selected_index = 4;
        break;
    default:
        selected_index = 0;
        break;
    }
    lv_dropdown_set_selected(theme_dropdown, selected_index);

    // --- 传输模式设置 ---
    lv_obj_t* transfer_mode_group = lv_obj_create(content_container);
    lv_obj_set_width(transfer_mode_group, lv_pct(100));
    lv_obj_set_height(transfer_mode_group, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(transfer_mode_group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(transfer_mode_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(transfer_mode_group, 0, 0);
    lv_obj_set_style_pad_all(transfer_mode_group, 0, 0);
    lv_obj_set_style_pad_gap(transfer_mode_group, 10, 0);

    lv_obj_t* transfer_mode_label = lv_label_create(transfer_mode_group);
    lv_label_set_text(transfer_mode_label, text->transfer_mode);
    theme_apply_to_label(transfer_mode_label, false);
    set_language_display(transfer_mode_label);

    lv_obj_t* cb_container = lv_obj_create(transfer_mode_group);
    lv_obj_set_width(cb_container, lv_pct(100));
    lv_obj_set_height(cb_container, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cb_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cb_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(cb_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cb_container, 0, 0);
    lv_obj_set_style_pad_all(cb_container, 0, 0);

    lv_obj_t* udp_checkbox = lv_checkbox_create(cb_container);
    lv_checkbox_set_text(udp_checkbox, "UDP");
    lv_obj_t* tcp_checkbox = lv_checkbox_create(cb_container);
    lv_checkbox_set_text(tcp_checkbox, "TCP");

    image_transfer_mode_t current_mode = settings_get_transfer_mode();
    if (current_mode == IMAGE_TRANSFER_MODE_TCP) {
        lv_obj_add_state(tcp_checkbox, LV_STATE_CHECKED);
    } else {
        lv_obj_add_state(udp_checkbox, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(tcp_checkbox, transfer_mode_tcp_cb, LV_EVENT_VALUE_CHANGED, udp_checkbox);
    lv_obj_add_event_cb(udp_checkbox, transfer_mode_udp_cb, LV_EVENT_VALUE_CHANGED, tcp_checkbox);

    // --- WiFi设置 ---
    lv_obj_t* wifi_btn = lv_btn_create(content_container);
    lv_obj_set_width(wifi_btn, lv_pct(100));
    lv_obj_set_height(wifi_btn, 40);
    theme_apply_to_button(wifi_btn, true);
    lv_obj_add_event_cb(wifi_btn, wifi_settings_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* wifi_label = lv_label_create(wifi_btn);
    lv_label_set_text(wifi_label, text->wifi_settings_label);
    set_language_display(wifi_label);
    theme_apply_to_label(wifi_label, false);
    lv_obj_center(wifi_label);
    set_language_display(wifi_label);

    // --- 自动配对 ---
    lv_obj_t* pairing_btn = lv_btn_create(content_container);
    lv_obj_set_width(pairing_btn, lv_pct(100));
    lv_obj_set_height(pairing_btn, 40);
    theme_apply_to_button(pairing_btn, true);
    lv_obj_add_event_cb(pairing_btn, auto_pairing_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* pairing_label = lv_label_create(pairing_btn);
    lv_label_set_text(pairing_label, text->auto_pairing_label);
    theme_apply_to_label(pairing_label, false);
    lv_obj_center(pairing_label);
    set_language_display(pairing_label);

    // --- 关于 ---
    lv_obj_t* about_btn = lv_btn_create(content_container);
    lv_obj_set_width(about_btn, lv_pct(100));
    lv_obj_set_height(about_btn, 40);
    theme_apply_to_button(about_btn, true);
    lv_obj_add_event_cb(about_btn, about_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* about_label = lv_label_create(about_btn);
    lv_label_set_text(about_label, text->about_label);
    theme_apply_to_label(about_label, false);
    lv_obj_center(about_label);
    set_language_display(about_label);

    ESP_LOGI(TAG, "Settings UI created with language: %s",
             g_current_language == LANG_CHINESE ? "Chinese" : "English");
}

static void transfer_mode_tcp_cb(lv_event_t* e) {
    lv_obj_t* tcp_cb = lv_event_get_target(e);
    lv_obj_t* udp_cb = (lv_obj_t*)lv_event_get_user_data(e);

    if (lv_obj_has_state(tcp_cb, LV_STATE_CHECKED)) {
        lv_obj_clear_state(udp_cb, LV_STATE_CHECKED);
        settings_set_transfer_mode(IMAGE_TRANSFER_MODE_TCP);
        lv_event_send(lv_scr_act(), UI_EVENT_SETTINGS_CHANGED, NULL);
    } else {
        // Prevent unchecking if the other is also unchecked
        if (!lv_obj_has_state(udp_cb, LV_STATE_CHECKED)) {
            lv_obj_add_state(tcp_cb, LV_STATE_CHECKED);
        }
    }
}

static void transfer_mode_udp_cb(lv_event_t* e) {
    lv_obj_t* udp_cb = lv_event_get_target(e);
    lv_obj_t* tcp_cb = (lv_obj_t*)lv_event_get_user_data(e);

    if (lv_obj_has_state(udp_cb, LV_STATE_CHECKED)) {
        lv_obj_clear_state(tcp_cb, LV_STATE_CHECKED);
        settings_set_transfer_mode(IMAGE_TRANSFER_MODE_UDP);
        lv_event_send(lv_scr_act(), UI_EVENT_SETTINGS_CHANGED, NULL);
    } else {
        // Prevent unchecking if the other is also unchecked
        if (!lv_obj_has_state(tcp_cb, LV_STATE_CHECKED)) {
            lv_obj_add_state(udp_cb, LV_STATE_CHECKED);
        }
    }
}

// 获取当前语言设置
ui_language_t ui_get_current_language(void) { return g_current_language; }

// 设置语言
void ui_set_language(ui_language_t lang) {
    g_current_language = lang;
    save_language_setting(lang);
}