/**
 * @file ui_common.c
 * @brief 通用UI组件
 * @author TidyCraze
 * @date 2025-01-27
 */

#include "ui_common.h"
#include "esp_log.h"
#include "font/lv_symbol_def.h"
#include "game.h"
#include "settings_manager.h"
#include "ui.h"
#include "my_font.h"
#include "ui_state_manager.h"

static const char* TAG = "UI_COMMON";

// Forward declaration for the unified settings button callback
static void common_settings_btn_callback(lv_event_t* e);

// 统一的返回按钮回调函数 - 返回到主菜单
static void back_button_callback(lv_event_t* e) {
    lv_obj_t* screen = lv_scr_act();
    if (screen) {
        lv_obj_clean(screen);
        ui_main_menu_create(screen);
    }
}

// 返回到游戏菜单的回调函数
static void back_to_game_menu_callback(lv_event_t* e) {
    lv_obj_t* screen = lv_scr_act();
    if (screen) {
        lv_obj_clean(screen);
        // 直接调用游戏菜单创建函数
        extern void ui_game_menu_create(lv_obj_t * parent);
        ui_game_menu_create(screen);
    }
}

// 创建统一的back按钮 - 返回到主菜单
void ui_create_back_button(lv_obj_t* parent, const char* text) {
    (void)text; // 忽略text参数，统一使用符号
    // 创建back按钮 - 统一放置在左上角，与页面标题对齐
    lv_obj_t* back_btn = lv_btn_create(parent);
    lv_obj_set_size(back_btn, 40, 40);                 // 使用符号后可以更小
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 10, 10); // 左上角，与标题区域对齐

    // 设置按钮样式
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x666666), 0); // 灰色背景
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_80, 0);
    lv_obj_set_style_radius(back_btn, 6, 0);       // 圆角
    lv_obj_set_style_shadow_width(back_btn, 2, 0); // 轻微阴影
    lv_obj_set_style_shadow_ofs_y(back_btn, 1, 0);
    lv_obj_set_style_shadow_opa(back_btn, LV_OPA_30, 0);

    // 添加点击事件
    lv_obj_add_event_cb(back_btn, back_button_callback, LV_EVENT_CLICKED, NULL);

    // 创建按钮标签
    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_16, 0);  // 符号字体稍大一些
    lv_obj_set_style_text_color(back_label, lv_color_hex(0xFFFFFF), 0); // 白色文字
    lv_obj_center(back_label);

    ESP_LOGI(TAG, "Back button created at top-left position");
}

// 创建返回到游戏菜单的back按钮
void ui_create_game_back_button(lv_obj_t* parent, const char* text) {
    (void)text; // 忽略text参数，统一使用符号
    // 创建back按钮 - 统一放置在左上角，与页面标题对齐
    lv_obj_t* back_btn = lv_btn_create(parent);
    lv_obj_set_size(back_btn, 40, 30);                 // 使用符号后可以更小
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 10, 10); // 左上角，与标题区域对齐

    // 设置按钮样式
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x666666), 0); // 灰色背景
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_80, 0);
    lv_obj_set_style_radius(back_btn, 6, 0);       // 圆角
    lv_obj_set_style_shadow_width(back_btn, 2, 0); // 轻微阴影
    lv_obj_set_style_shadow_ofs_y(back_btn, 1, 0);
    lv_obj_set_style_shadow_opa(back_btn, LV_OPA_30, 0);

    // 添加点击事件 - 返回到游戏菜单
    lv_obj_add_event_cb(back_btn, back_to_game_menu_callback, LV_EVENT_CLICKED, NULL);

    // 创建按钮标签
    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_16, 0);  // 符号字体稍大一些
    lv_obj_set_style_text_color(back_label, lv_color_hex(0xFFFFFF), 0); // 白色文字
    lv_obj_center(back_label);

    ESP_LOGI(TAG, "Game back button created at top-left position");
}

// 创建页面父级容器（统一管理整个页面）
void ui_create_page_parent_container(lv_obj_t* parent, lv_obj_t** page_parent_container) {
    // 创建页面父级容器 - 占满整个屏幕
    *page_parent_container = lv_obj_create(parent);
    lv_obj_set_size(*page_parent_container, 240, 320); // 占满屏幕
    lv_obj_align(*page_parent_container, LV_ALIGN_CENTER, 0, 0);

    // 设置父级容器样式 - 不可滚动
    lv_obj_set_style_bg_opa(*page_parent_container, LV_OPA_0, LV_PART_MAIN); // 透明背景
    lv_obj_set_style_border_width(*page_parent_container, 0, LV_PART_MAIN);  // 无边框
    lv_obj_set_style_pad_all(*page_parent_container, 0, LV_PART_MAIN);       // 无内边距
    lv_obj_set_style_radius(*page_parent_container, 0, LV_PART_MAIN);        // 无圆角
    lv_obj_clear_flag(*page_parent_container, LV_OBJ_FLAG_SCROLLABLE);       // 禁止滚动

    ESP_LOGI(TAG, "Page parent container created");
}

// 创建顶部栏容器（包含返回按钮、标题和设置按钮）
void ui_create_top_bar(lv_obj_t* parent, const char* title_text, bool show_settings_btn, lv_obj_t** top_bar_container,
                       lv_obj_t** title_container, lv_obj_t** settings_btn_out) {
    // 创建顶部栏容器 - 240x30
    *top_bar_container = lv_obj_create(parent);
    lv_obj_set_size(*top_bar_container, 240, 30);
    lv_obj_align(*top_bar_container, LV_ALIGN_TOP_MID, 0, 0);

    // 设置顶部栏容器的样式 - 主题表面色
    lv_obj_set_style_bg_color(*top_bar_container, theme_get_color(theme_get_current_theme()->colors.surface),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(*top_bar_container, LV_OPA_100, LV_PART_MAIN); // 不透明背景
    lv_obj_set_style_border_width(*top_bar_container, 0, LV_PART_MAIN);    // 无边框
    lv_obj_set_style_pad_all(*top_bar_container, 0, LV_PART_MAIN);         // 无内边距
    lv_obj_set_style_radius(*top_bar_container, 0, LV_PART_MAIN);          // 无圆角
    lv_obj_clear_flag(*top_bar_container, LV_OBJ_FLAG_SCROLLABLE);         // 禁止滚动

    // 创建返回按钮 - 左侧放置，大小40x30
    lv_obj_t* back_btn = lv_btn_create(*top_bar_container);
    lv_obj_set_size(back_btn, 40, 30);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 0, 0);

    // 设置返回按钮样式
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x666666), 0);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_80, 0);
    lv_obj_set_style_radius(back_btn, 6, 0);
    lv_obj_set_style_shadow_width(back_btn, 2, 0);
    lv_obj_set_style_shadow_ofs_y(back_btn, 1, 0);
    lv_obj_set_style_shadow_opa(back_btn, LV_OPA_30, 0);

    // 添加返回按钮点击事件
    lv_obj_add_event_cb(back_btn, back_button_callback, LV_EVENT_CLICKED, NULL);

    // 创建返回按钮标签
    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(back_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(back_label);

    // 创建标题容器
    *title_container = lv_obj_create(*top_bar_container);
    lv_obj_set_size(*title_container, 160, 30);
    lv_obj_align(*title_container, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(*title_container, LV_OPA_0, 0);
    lv_obj_set_style_border_width(*title_container, 0, 0);
    lv_obj_set_style_pad_all(*title_container, 0, 0);

    // 创建标题
    lv_obj_t* title = lv_label_create(*title_container);
    lv_label_set_text(title, title_text);
    set_language_display(title);
    // lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // Conditionally create the settings button
    if (show_settings_btn) {
        lv_obj_t* settings_btn = lv_btn_create(*top_bar_container);
        lv_obj_set_size(settings_btn, 40, 30);
        lv_obj_align(settings_btn, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_color(settings_btn, lv_color_hex(0x666666), 0);
        lv_obj_set_style_bg_opa(settings_btn, LV_OPA_80, 0);
        lv_obj_set_style_radius(settings_btn, 6, 0);
        lv_obj_set_style_shadow_width(settings_btn, 2, 0);
        lv_obj_set_style_shadow_ofs_y(settings_btn, 1, 0);
        lv_obj_set_style_shadow_opa(settings_btn, LV_OPA_30, 0);

        // Return the button object if the caller wants it
        if (settings_btn_out) {
            *settings_btn_out = settings_btn;
        }
    } else {
        // Ensure the output pointer is NULL if no button is created
        if (settings_btn_out) {
            *settings_btn_out = NULL;
        }
    }

    ESP_LOGI(TAG, "Top bar created: %s", title_text);
}

// 创建页面内容容器（除开顶部栏的区域）
void ui_create_page_content_area(lv_obj_t* parent, lv_obj_t** content_container) {
    // 创建页面内容容器 - 240x290，从顶部栏下方开始
    *content_container = lv_obj_create(parent);
    lv_obj_set_size(*content_container, 240, 290);             // 高度为屏幕高度减去顶部栏高度 (320-30=290)
    lv_obj_align(*content_container, LV_ALIGN_TOP_MID, 0, 30); // 从顶部栏下方开始

    // 设置内容区域容器的样式 - 主题背景色，可滚动
    lv_obj_set_style_bg_color(*content_container, theme_get_color(theme_get_current_theme()->colors.background),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(*content_container, LV_OPA_100, LV_PART_MAIN); // 不透明背景
    lv_obj_set_style_border_width(*content_container, 0, LV_PART_MAIN);    // 无边框
    lv_obj_set_style_pad_all(*content_container, 0, LV_PART_MAIN);         // 移除内边距，避免横向滚动
    lv_obj_set_style_radius(*content_container, 0, LV_PART_MAIN);          // 无圆角
    lv_obj_add_flag(*content_container, LV_OBJ_FLAG_SCROLLABLE);           // 允许滚动

    ESP_LOGI(TAG, "Page content area created (scrollable)");
}

// 创建统一的页面标题（保留原有函数以兼容性）
void ui_create_page_title(lv_obj_t* parent, const char* title_text) {
    // 创建标题容器 - 统一高度30像素
    lv_obj_t* title_container = lv_obj_create(parent);
    lv_obj_set_size(title_container, 240, 30);
    lv_obj_align(title_container, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_bg_opa(title_container, LV_OPA_0, LV_PART_MAIN); // 透明背景
    lv_obj_set_style_border_width(title_container, 0, 0);
    lv_obj_set_style_pad_all(title_container, 0, 0);
    lv_obj_clear_flag(title_container, LV_OBJ_FLAG_SCROLLABLE); // 禁止滚动

    // 创建标题标签
    lv_obj_t* title = lv_label_create(title_container);
    lv_label_set_text(title, title_text);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);                   // 居中对齐
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0); // 统一使用20号字体
    lv_obj_set_style_text_color(title, theme_get_color(theme_get_current_theme()->colors.text_primary), 0);
    lv_obj_clear_flag(title, LV_OBJ_FLAG_SCROLLABLE); // 禁止滚动

    ESP_LOGI(TAG, "Page title created: %s", title_text);
}

// 图传模式设置 - 全局变量
static lv_obj_t* settings_popup = NULL;

// The unified callback for settings button is now removed.
// Each page will define its own callback.

// 设置弹出窗口回调函数
static void settings_popup_close_callback(lv_event_t* e) {
    if (settings_popup) {
        lv_obj_del(settings_popup);
        settings_popup = NULL;
    }
}

static void tcp_checkbox_callback(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* obj = lv_event_get_target(e);

    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t* udp_checkbox = (lv_obj_t*)lv_event_get_user_data(e);
        if (lv_obj_has_state(obj, LV_STATE_CHECKED)) {
            lv_obj_clear_state(udp_checkbox, LV_STATE_CHECKED);
            settings_set_transfer_mode(IMAGE_TRANSFER_MODE_TCP);
            lv_event_send(lv_scr_act(), UI_EVENT_SETTINGS_CHANGED, NULL);
        } else {
            // Prevent unchecking both
            if (!lv_obj_has_state(udp_checkbox, LV_STATE_CHECKED)) {
                lv_obj_add_state(obj, LV_STATE_CHECKED);
            }
        }
    }
}

static void udp_checkbox_callback(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* obj = lv_event_get_target(e);

    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t* tcp_checkbox = (lv_obj_t*)lv_event_get_user_data(e);
        if (lv_obj_has_state(obj, LV_STATE_CHECKED)) {
            lv_obj_clear_state(tcp_checkbox, LV_STATE_CHECKED);
            settings_set_transfer_mode(IMAGE_TRANSFER_MODE_UDP);
            lv_event_send(lv_scr_act(), UI_EVENT_SETTINGS_CHANGED, NULL);
        } else {
            // Prevent unchecking both
            if (!lv_obj_has_state(tcp_checkbox, LV_STATE_CHECKED)) {
                lv_obj_add_state(obj, LV_STATE_CHECKED);
            }
        }
    }
}

// 创建设置弹出窗口
void ui_create_settings_popup(lv_obj_t* parent) {
    if (settings_popup) {
        return; // 避免重复创建
    }

    // 创建弹出窗口背景遮罩
    settings_popup = lv_obj_create(parent);
    lv_obj_set_size(settings_popup, LV_PCT(100), LV_PCT(100));
    lv_obj_align(settings_popup, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(settings_popup, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(settings_popup, LV_OPA_50, 0);
    lv_obj_clear_flag(settings_popup, LV_OBJ_FLAG_SCROLLABLE);

    // 创建设置对话框
    lv_obj_t* dialog = lv_obj_create(settings_popup);
    lv_obj_set_size(dialog, 200, 150);
    lv_obj_align(dialog, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(dialog, theme_get_color(theme_get_current_theme()->colors.surface), 0);
    lv_obj_set_style_border_width(dialog, 2, 0);
    lv_obj_set_style_border_color(dialog, theme_get_color(theme_get_current_theme()->colors.border), 0);
    lv_obj_set_style_radius(dialog, 10, 0);
    lv_obj_clear_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);

    // 设置标题
    lv_obj_t* title = lv_label_create(dialog);
    lv_label_set_text(title, "Settings");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);

    // Create UDP Checkbox first to pass as user data
    lv_obj_t* udp_checkbox = lv_checkbox_create(dialog);
    lv_checkbox_set_text(udp_checkbox, "UDP Mode");
    lv_obj_align(udp_checkbox, LV_ALIGN_LEFT_MID, 15, 15);
    lv_obj_set_style_text_font(udp_checkbox, &lv_font_montserrat_14, 0);

    // 创建TCP Checkbox
    lv_obj_t* tcp_checkbox = lv_checkbox_create(dialog);
    lv_checkbox_set_text(tcp_checkbox, "TCP Mode");
    lv_obj_align(tcp_checkbox, LV_ALIGN_LEFT_MID, 15, -10);
    lv_obj_set_style_text_font(tcp_checkbox, &lv_font_montserrat_14, 0);

    // 设置初始状态
    image_transfer_mode_t current_mode = settings_get_transfer_mode();
    if (current_mode == IMAGE_TRANSFER_MODE_TCP) {
        lv_obj_add_state(tcp_checkbox, LV_STATE_CHECKED);
    } else {
        lv_obj_add_state(udp_checkbox, LV_STATE_CHECKED);
    }

    // 添加事件回调（互斥选择）
    lv_obj_add_event_cb(tcp_checkbox, tcp_checkbox_callback, LV_EVENT_VALUE_CHANGED, udp_checkbox);
    lv_obj_add_event_cb(udp_checkbox, udp_checkbox_callback, LV_EVENT_VALUE_CHANGED, tcp_checkbox);

    // 创建关闭按钮
    lv_obj_t* close_btn = lv_btn_create(dialog);
    lv_obj_set_size(close_btn, 60, 30);
    lv_obj_align(close_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_add_event_cb(close_btn, settings_popup_close_callback, LV_EVENT_CLICKED, NULL);

    lv_obj_t* close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, "Close");
    lv_obj_center(close_label);
    lv_obj_set_style_text_font(close_label, &lv_font_montserrat_14, 0);

    // 添加背景点击关闭功能
    lv_obj_add_event_cb(settings_popup, settings_popup_close_callback, LV_EVENT_CLICKED, NULL);

    ESP_LOGI(TAG, "Settings popup created");
}

// This getter function is no longer needed here. It's in settings_manager.
// image_transfer_mode_t ui_get_image_transfer_mode(void) {
//     return g_image_transfer_mode;
// }

// 创建支持状态恢复的返回按钮
void ui_create_stateful_back_button(lv_obj_t* parent) {
    // 创建back按钮 - 统一放置在左上角，与页面标题对齐
    lv_obj_t* back_btn = lv_btn_create(parent);
    lv_obj_set_size(back_btn, 40, 40);                 // 使用符号后可以更小
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 10, 10); // 左上角，与标题区域对齐

    // 设置按钮样式
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x666666), 0); // 灰色背景
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_80, 0);
    lv_obj_set_style_radius(back_btn, 6, 0);       // 圆角
    lv_obj_set_style_shadow_width(back_btn, 2, 0); // 轻微阴影
    lv_obj_set_style_shadow_ofs_y(back_btn, 1, 0);
    lv_obj_set_style_shadow_opa(back_btn, LV_OPA_30, 0);

    // 添加点击事件 - 使用支持状态恢复的回调
    lv_obj_add_event_cb(back_btn, back_button_callback, LV_EVENT_CLICKED, NULL);

    // 创建按钮标签
    lv_obj_t* back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_16, 0);  // 符号字体稍大一些
    lv_obj_set_style_text_color(back_label, lv_color_hex(0xFFFFFF), 0); // 白色文字
    lv_obj_center(back_label);

}

// 全局语言设置
ui_language_t g_current_language = LANG_ENGLISH;

// 获取当前字体
const lv_font_t* get_current_font(void) {
    if (g_current_language == LANG_CHINESE) {
        return get_loaded_font();
    }
    return &lv_font_montserrat_16;
}

const void set_language_display(lv_obj_t* obj) {
    lv_font_t* font = get_current_font();
    lv_obj_set_style_text_font(obj, font, 0);
}
