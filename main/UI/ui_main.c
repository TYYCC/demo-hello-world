/*******************************************************************************
 * @file           : {fileName}
 * @brief          :
 * @author         : {author}
 * @date           : {createTime}
 * @version        : 1.0
 ******************************************************************************/
/*
 * Author: tidycraze 2595256284@qq.com
 * Date: 2025-09-01 13:24:54
 * LastEditors: tidycraze 2595256284@qq.com
 * LastEditTime: 2025-09-08 15:45:09
 * FilePath: \demo-hello-world\main\UI\ui_main.c
 * Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置:
 * https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
/**
 * @file ui_main.c
 * @brief 主界面UI
 * @author TidyCraze
 * @date 2025-08-14
 */

#include "../app/game/game.h"
#include "background_manager.h"
#include "color.h"
#include "esp_log.h"
#include "font/lv_font.h"
#include "my_font.h"
#include "status_bar_manager.h"
#include "theme_manager.h"
#include "ui.h"
#include "ui_calibration.h"
#include "ui_image_transfer.h"
#include "ui_serial_display.h"
#include "ui_state_manager.h" // 添加状态管理器头文件
#include "ui_telemetry.h"     // 添加遥测UI头文件
#include "ui_test.h"
// #include "wifi_image_transfer.h"  // 文件不存在，暂时注释掉
#include "wifi_manager.h"

// audio receiver is declared in ui.h (ui_audio_receiver_create)

// 全局变量保存时间标签和电池标签
static lv_obj_t* g_time_label = NULL;
static lv_obj_t* g_battery_label = NULL;

// 全局变量保存菜单容器和当前选中状态
static lv_obj_t* g_menu_container = NULL;
static int g_current_selected_index = 0;

// 函数声明

// 时间更新定时器回调函数
static void time_update_timer_cb(lv_timer_t* timer) {
    if (!g_time_label) {
        ESP_LOGW("UI_MAIN", "Time label is NULL!");
        return;
    }

    // 检查标签是否仍然有效
    if (!lv_obj_is_valid(g_time_label)) {
        ESP_LOGW("UI_MAIN", "Time label is no longer valid!");
        g_time_label = NULL; // 重置为NULL
        return;
    }

    // 检查后台时间是否有更新
    if (background_manager_is_time_changed()) {
        char time_str[32];
        if (background_manager_get_time_str(time_str, sizeof(time_str)) == ESP_OK) {
            // 更新UI显示
            lv_label_set_text(g_time_label, time_str);
            lv_obj_invalidate(g_time_label);
            // 标记已显示
            background_manager_mark_time_displayed();
        }
    }
}

// 电池电量更新函数（由任务调用）
void ui_main_update_battery_display(void) {
    if (!g_battery_label) {
        ESP_LOGW("UI_MAIN", "Battery label is NULL!");
        return;
    }

    // 检查标签是否仍然有效
    if (!lv_obj_is_valid(g_battery_label)) {
        ESP_LOGW("UI_MAIN", "Battery label is no longer valid!");
        g_battery_label = NULL; // 重置为NULL
        return;
    }

    // 检查后台电池信息是否有更新
    if (background_manager_is_battery_changed()) {
        char battery_str[32];
        background_battery_info_t battery_info;

        if (background_manager_get_battery_str(battery_str, sizeof(battery_str)) == ESP_OK &&
            background_manager_get_battery(&battery_info) == ESP_OK) {

            // 更新文字
            lv_label_set_text(g_battery_label, battery_str);

            // 根据电量设置颜色
            if (battery_info.percentage <= 30) {
                // 30%以下显示红色
                lv_obj_set_style_text_color(g_battery_label, lv_color_hex(0xFF0000), 0);
            } else {
                // 30%以上显示黑色
                lv_obj_set_style_text_color(g_battery_label, lv_color_hex(0x000000), 0);
            }

            // 标记已显示
            background_manager_mark_battery_displayed();
            // 电池状态更新
        }
    }
}

// 菜单项回调函数类型
typedef void (*menu_item_cb_t)(void);

// 示例回调函数（后续扩展时实现）
static void settings_cb(void) {
    // 保存主菜单状态
    if (g_menu_container) {
        int scroll_pos = lv_obj_get_scroll_y(g_menu_container);
        ui_state_manager_save_main_menu(g_menu_container, g_current_selected_index, scroll_pos);
        ui_state_manager_save_current_screen(UI_SCREEN_SETTINGS);
    }

    lv_obj_t* screen = lv_scr_act();
    if (screen) {
        // 先停止时间更新定时器，避免访问已删除的UI元素
        static lv_timer_t* timer = NULL;
        if (timer) {
            lv_timer_del(timer);
            timer = NULL;
            ESP_LOGI("UI_MAIN", "Time update timer stopped");
        }

        // 反初始化状态栏管理器
        status_bar_manager_deinit();

        // 重置全局UI指针
        g_time_label = NULL;
        g_battery_label = NULL;
        g_menu_container = NULL;

        lv_obj_clean(screen);       // 清空当前屏幕
        ui_settings_create(screen); // 加载系统设置界面
    }
}

static void game_cb(void) {
    // 保存主菜单状态
    if (g_menu_container) {
        int scroll_pos = lv_obj_get_scroll_y(g_menu_container);
        ui_state_manager_save_main_menu(g_menu_container, g_current_selected_index, scroll_pos);
        ui_state_manager_save_current_screen(UI_SCREEN_GAME);
    }

    lv_obj_t* screen = lv_scr_act();
    if (screen) {
        // 反初始化状态栏管理器
        status_bar_manager_deinit();

        // 重置全局UI指针
        g_time_label = NULL;
        g_battery_label = NULL;
        g_menu_container = NULL;

        lv_obj_clean(screen);        // 清空当前屏幕
        ui_game_menu_create(screen); // 加载游戏子菜单界面
    }
}

static void image_transfer_cb(void) {
    // 保存主菜单状态
    if (g_menu_container) {
        int scroll_pos = lv_obj_get_scroll_y(g_menu_container);
        ui_state_manager_save_main_menu(g_menu_container, g_current_selected_index, scroll_pos);
        ui_state_manager_save_current_screen(UI_SCREEN_IMAGE_TRANSFER);
    }

    lv_obj_t* screen = lv_scr_act();
    if (screen) {
        // 反初始化状态栏管理器
        status_bar_manager_deinit();

        // 重置全局UI指针
        g_time_label = NULL;
        g_battery_label = NULL;
        g_menu_container = NULL;

        lv_obj_clean(screen);             // 清空当前屏幕
        ui_image_transfer_create(screen); // 加载图传界面
    }
}

static void serial_display_cb(void) {
    // 保存主菜单状态
    if (g_menu_container) {
        int scroll_pos = lv_obj_get_scroll_y(g_menu_container);
        ui_state_manager_save_main_menu(g_menu_container, g_current_selected_index, scroll_pos);
        ui_state_manager_save_current_screen(UI_SCREEN_SERIAL_DISPLAY);
    }

    lv_obj_t* screen = lv_scr_act();
    if (screen) {
        // 先停止时间更新定时器，避免访问已删除的UI元素
        static lv_timer_t* timer = NULL;
        if (timer) {
            lv_timer_del(timer);
            timer = NULL;
            ESP_LOGI("UI_MAIN", "Time update timer stopped");
        }

        // 反初始化状态栏管理器
        status_bar_manager_deinit();

        // 重置全局UI指针
        g_time_label = NULL;
        g_battery_label = NULL;
        g_menu_container = NULL;

        lv_obj_clean(screen);             // 清空当前屏幕
        ui_serial_display_create(screen); // 加载串口显示界面
    }
}

static void calibration_cb(void) {
    // 保存主菜单状态
    if (g_menu_container) {
        int scroll_pos = lv_obj_get_scroll_y(g_menu_container);
        ui_state_manager_save_main_menu(g_menu_container, g_current_selected_index, scroll_pos);
        ui_state_manager_save_current_screen(UI_SCREEN_CALIBRATION);
    }

    lv_obj_t* screen = lv_scr_act();
    if (screen) {
        // 反初始化状态栏管理器
        status_bar_manager_deinit();

        // 重置全局UI指针
        g_time_label = NULL;
        g_battery_label = NULL;
        g_menu_container = NULL;

        lv_obj_clean(screen);          // 清空当前屏幕
        ui_calibration_create(screen); // 加载校准界面
    }
}

static void test_cb(void) {
    // 保存主菜单状态
    if (g_menu_container) {
        int scroll_pos = lv_obj_get_scroll_y(g_menu_container);
        ui_state_manager_save_main_menu(g_menu_container, g_current_selected_index, scroll_pos);
        ui_state_manager_save_current_screen(UI_SCREEN_TEST);
    }

    lv_obj_t* screen = lv_scr_act();
    if (screen) {
        // 先停止时间更新定时器，避免访问已删除的UI元素
        static lv_timer_t* timer = NULL;
        if (timer) {
            lv_timer_del(timer);
            timer = NULL;
            ESP_LOGI("UI_MAIN", "Time update timer stopped");
        }

        // 反初始化状态栏管理器
        status_bar_manager_deinit();

        // 重置全局UI指针
        g_time_label = NULL;
        g_battery_label = NULL;
        g_menu_container = NULL;

        lv_obj_clean(screen);   // 清空当前屏幕
        ui_test_create(screen); // 加载测试界面
    }
}

static void telemetry_cb(void) {
    // 保存主菜单状态
    if (g_menu_container) {
        int scroll_pos = lv_obj_get_scroll_y(g_menu_container);
        ui_state_manager_save_main_menu(g_menu_container, g_current_selected_index, scroll_pos);
        ui_state_manager_save_current_screen(UI_SCREEN_TELEMETRY);
    }

    lv_obj_t* screen = lv_scr_act();
    if (screen) {
        // 先停止时间更新定时器，避免访问已删除的UI元素
        static lv_timer_t* timer = NULL;
        if (timer) {
            lv_timer_del(timer);
            timer = NULL;
            ESP_LOGI("UI_MAIN", "Time update timer stopped");
        }

        // 反初始化状态栏管理器
        status_bar_manager_deinit();

        // 重置全局UI指针
        g_time_label = NULL;
        g_battery_label = NULL;
        g_menu_container = NULL;

        lv_obj_clean(screen);        // 清空当前屏幕
        ui_telemetry_create(screen); // 加载遥测界面
    }
}

// 可扩展的菜单项结构
typedef struct {
    const char* text;
    menu_item_cb_t callback;
} menu_item_t;

// 菜单项数组
static menu_item_t menu_items[] = {
    {"Image Transfer", image_transfer_cb},
    {"Serial Display", serial_display_cb},
    {"Remote Control", telemetry_cb},
    {"Game", game_cb},
    {"Calibration", calibration_cb},
    {"Settings", settings_cb}
    // {"Test", test_cb},
    // 添加更多项...
};

static menu_item_t menu_items_zh[] = {
    {"图传", image_transfer_cb}, 
    {"串口", serial_display_cb},
    {"遥测", telemetry_cb},   
    {"游戏", game_cb},
    {"校准", calibration_cb},           
    {"设置", settings_cb}
    // {"测试", test_cb}
};

static void btn_event_cb(lv_event_t* e) {
    lv_obj_t* btn = lv_event_get_target(e);
    menu_item_cb_t cb = (menu_item_cb_t)lv_event_get_user_data(e);

    // 获取按钮在菜单中的索引
    if (g_menu_container) {
        uint32_t child_count = lv_obj_get_child_cnt(g_menu_container);
        for (uint32_t i = 0; i < child_count; i++) {
            if (lv_obj_get_child(g_menu_container, i) == btn) {
                g_current_selected_index = i;
                break;
            }
        }
    }

    if (cb)
        cb();
}

void ui_main_menu_create(lv_obj_t* parent) {
    // 初始化状态栏管理器
    status_bar_manager_init();

    // 标记当前屏幕类型
    ui_state_manager_save_current_screen(UI_SCREEN_MAIN_MENU);

    // 检查是否需要恢复状态
    ui_main_menu_state_t* saved_state = ui_state_manager_get_main_menu_state();

    // 应用当前主题到屏幕
    theme_apply_to_screen(parent);

    // 创建顶部状态栏
    lv_obj_t* status_bar = lv_obj_create(parent);
    lv_obj_set_size(status_bar, 240, 26);
    lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(status_bar, LV_OPA_0, 0); // 透明背景
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_set_style_pad_all(status_bar, 0, 0);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE); // 禁止滚动

    // 创建时间显示标签 - 在最左边
    g_time_label = lv_label_create(status_bar);
    lv_obj_set_style_text_font(g_time_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(g_time_label, lv_color_hex(0x000000), 0); // 黑色
    lv_obj_align(g_time_label, LV_ALIGN_LEFT_MID, 6, 0);
    lv_label_set_text(g_time_label, "00:00");

    // ==== 电池图标（外壳 + 小凸起 + 电量文字） ====
    // 电池外壳
    lv_obj_t* battery_icon = lv_obj_create(status_bar);
    lv_obj_set_size(battery_icon, 28, 16);
    lv_obj_align(battery_icon, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_set_style_bg_color(battery_icon, lv_color_hex(0x000000), 0); // 黑色边框
    lv_obj_set_style_bg_opa(battery_icon, LV_OPA_0, 0);                 // 透明填充
    lv_obj_set_style_border_width(battery_icon, 2, 0);
    lv_obj_set_style_border_color(battery_icon, lv_color_hex(0x000000), 0);
    lv_obj_set_style_radius(battery_icon, 2, 0);
    lv_obj_clear_flag(battery_icon, LV_OBJ_FLAG_SCROLLABLE);

    // 电池正极（小凸起），直接以电池外壳为参考对齐
    lv_obj_t* battery_positive = lv_obj_create(status_bar);
    lv_obj_set_size(battery_positive, 4, 8);
    lv_obj_align_to(battery_positive, battery_icon, LV_ALIGN_OUT_RIGHT_MID, 1, 0);
    lv_obj_set_style_bg_color(battery_positive, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(battery_positive, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(battery_positive, 2, 0);
    lv_obj_set_style_border_width(battery_positive, 0, 0);
    lv_obj_clear_flag(battery_positive, LV_OBJ_FLAG_SCROLLABLE);

    // 电池电量显示标签 - 在电池外壳中间
    g_battery_label = lv_label_create(battery_icon);
    lv_obj_set_style_text_font(g_battery_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(g_battery_label, lv_color_hex(0x000000), 0); // 黑色
    lv_obj_align(g_battery_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(g_battery_label, "100");

    // 创建标题区域 - 在状态栏下方，透明背景
    lv_obj_t* title_container = lv_obj_create(parent);
    lv_obj_set_size(title_container, 240, 38);
    lv_obj_align(title_container, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_bg_opa(title_container, LV_OPA_0, LV_PART_MAIN); // 透明背景
    lv_obj_set_style_border_width(title_container, 0, 0);
    lv_obj_set_style_pad_all(title_container, 8, 0);
    lv_obj_clear_flag(title_container, LV_OBJ_FLAG_SCROLLABLE); // 禁止滚动

    // 创建主标题 - 靠左显示，字体放大加粗，禁止滚动
    lv_obj_t* title = lv_label_create(title_container);
    lv_label_set_text(title, "Browse");
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 10, 0);    // 靠左显示，左边距10像素
    theme_apply_to_label(title, true);                // 应用主题到标题
    lv_obj_clear_flag(title, LV_OBJ_FLAG_SCROLLABLE); // 禁止滚动

    // 根据当前语言选择菜单项
    ui_language_t current_language = ui_get_current_language();
    menu_item_t* current_menu_items =
        (current_language == LANG_CHINESE) ? menu_items_zh : menu_items;
    int num_items = (current_language == LANG_CHINESE)
                        ? (sizeof(menu_items_zh) / sizeof(menu_item_t))
                        : (sizeof(menu_items) / sizeof(menu_item_t));

    // 创建菜单按钮容器
    g_menu_container = lv_obj_create(parent);
    lv_obj_set_size(g_menu_container, 220, 220);
    lv_obj_align(g_menu_container, LV_ALIGN_CENTER, 0, 44); // 调整位置，避开状态栏和标题
    lv_obj_set_style_bg_opa(g_menu_container, LV_OPA_0, 0);
    lv_obj_set_style_border_width(g_menu_container, 0, 0);
    lv_obj_set_style_pad_all(g_menu_container, 0, 0);
    lv_obj_set_style_width(g_menu_container, 0, LV_PART_SCROLLBAR);
    lv_obj_set_style_opa(g_menu_container, LV_OPA_0, LV_PART_SCROLLBAR);

    // 创建按钮 - 使用莫兰迪色系
    lv_font_t* zh_font = get_loaded_font();
    for (int i = 0; i < num_items; i++) {
        lv_obj_t* btn = lv_obj_create(g_menu_container);
        lv_obj_set_size(btn, 200, 54);                       // 增加按钮高度到60像素
        lv_obj_align(btn, LV_ALIGN_CENTER, 0, -80 + i * 70); // 调整间距，每个按钮间隔70像素
        lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, current_menu_items[i].callback);

        // 设置按钮样式（圆角、阴影等）
        lv_obj_set_style_radius(btn, 15, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(btn, 6, LV_PART_MAIN);
        lv_obj_set_style_shadow_ofs_y(btn, 3, LV_PART_MAIN);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_40, LV_PART_MAIN);
        lv_obj_set_style_shadow_color(btn, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_pad_all(btn, 12, LV_PART_MAIN);

        // 应用主题到按钮（只改变颜色）
        theme_apply_to_button(btn, true);

        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, current_menu_items[i].text);
        theme_apply_to_label(label, false); // 应用主题到标签
        lv_obj_center(label);
        if(current_language == LANG_CHINESE)
            lv_obj_set_style_text_font(label, zh_font, 0); // 使用自定义中文字体
        else
            lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    }

    // 创建时间更新定时器（每分钟检查一次后台更新）
    static lv_timer_t* timer = NULL;
    if (timer == NULL) {
        timer = lv_timer_create(time_update_timer_cb, 60000, NULL); // 1分钟检查一次
        ESP_LOGI("UI_MAIN", "Time update timer created (1min interval)");
    } else {
        // 如果定时器已存在，先删除再创建新的
        lv_timer_del(timer);
        timer = lv_timer_create(time_update_timer_cb, 60000, NULL);
        ESP_LOGI("UI_MAIN", "Time update timer recreated (1min interval)");
    }

    // 初始化显示
    char time_str[32];
    char battery_str[32];
    background_battery_info_t battery_info;

    if (background_manager_get_time_str(time_str, sizeof(time_str)) == ESP_OK) {
        lv_label_set_text(g_time_label, time_str);
    }
    if (background_manager_get_battery_str(battery_str, sizeof(battery_str)) == ESP_OK &&
        background_manager_get_battery(&battery_info) == ESP_OK) {
        lv_label_set_text(g_battery_label, battery_str);

        // 根据电量设置初始颜色
        if (battery_info.percentage <= 30) {
            lv_obj_set_style_text_color(g_battery_label, lv_color_hex(0xFF0000), 0);
        } else {
            lv_obj_set_style_text_color(g_battery_label, lv_color_hex(0x000000), 0);
        }
    }

    // 设置状态栏管理器的容器和UI组件
    esp_err_t ret = status_bar_manager_set_container(status_bar, NULL);
    if (ret == ESP_OK) {
        status_bar_manager_set_fixed_labels(g_time_label, g_battery_label);
        status_bar_manager_start(); // 启动状态栏更新定时器
        ESP_LOGI("UI_MAIN", "Status bar manager container set and UI components configured");
    } else {
        ESP_LOGE("UI_MAIN", "Failed to set status bar manager container: %s", esp_err_to_name(ret));
    }

    // 恢复状态（如果有保存的状态）
    if (saved_state) {
        // 恢复选中的菜单项索引
        g_current_selected_index = saved_state->selected_index;

        // 恢复滚动位置
        lv_obj_scroll_to_y(g_menu_container, saved_state->scroll_position, LV_ANIM_OFF);

        ESP_LOGI("UI_MAIN", "Main menu state restored: selected=%d, scroll=%d",
                 saved_state->selected_index, saved_state->scroll_position);

        // 可选：高亮显示之前选中的菜单项
        if (saved_state->selected_index >= 0 && saved_state->selected_index < num_items) {
            lv_obj_t* selected_btn =
                lv_obj_get_child(g_menu_container, saved_state->selected_index);
            if (selected_btn) {
                // 可以添加一些视觉效果来表示这是之前选中的项目
                // 例如：稍微改变按钮的透明度或边框
                lv_obj_set_style_bg_opa(selected_btn, LV_OPA_90, LV_PART_MAIN);
            }
        }
    } else {
        // 如果没有保存的状态，初始化为默认值
        g_current_selected_index = 0;
        ESP_LOGI("UI_MAIN", "Main menu created with default state");
    }

    ESP_LOGI("UI_MAIN", "Main menu created with background manager support");

    // 扩展提示：要添加新选项，在 menu_items 数组中添加新项，并实现对应的回调函数
}