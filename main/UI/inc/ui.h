/**
 * @file UI.h
 * @brief UI模块统一头文件
 * @author Your Name
 * @date 2024
 */
#ifndef UI_H
#define UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "theme_manager.h"
#include "ui_state_manager.h"

// 定义加载阶段枚举
typedef enum {
    UI_STAGE_INITIALIZING = 0,// 初始化阶段
    UI_STAGE_LOADING_COMPONENTS,// 加载组件阶段
    UI_STAGE_STARTING_SERVICES,// 启动服务阶段
    UI_STAGE_CONFIGURING_HARDWARE,// 配置硬件阶段
    UI_STAGE_ALMOST_READY,// 准备就绪阶段
    UI_STAGE_FINALIZING,// 最终阶段
    UI_STAGE_DONE
} ui_load_stage_t;

// --- LVGL 主任务 ---
// 这个任务初始化并运行LVGL的主循环
void lvgl_main_task(void* pvParameters);

// --- 启动动画 UI ---

/**
 * @brief 启动动画完成时的回调函数类型
 */
typedef void (*ui_start_anim_finished_cb_t)(void);

/**
 * @brief 创建并开始播放启动动画
 * @param parent    父对象，通常是 lv_scr_act()
 * @param finished_cb 动画播放完成时调用的回调函数
 */
void ui_start_animation_create(lv_obj_t* parent, ui_start_anim_finished_cb_t finished_cb);

/**
 * @brief 创建主菜单界面
 * @param parent 父对象，通常是 lv_scr_act()
 */
void ui_main_menu_create(lv_obj_t* parent);

/**
 * @brief 创建WiFi设置界面
 * @param parent 父对象，通常是 lv_scr_act()
 */
void ui_wifi_settings_create(lv_obj_t* parent);

/**
 * @brief 创建系统设置界面
 * @param parent 父对象，通常是 lv_scr_act()
 */
void ui_settings_create(lv_obj_t* parent);

/**
 * @brief 更新电池电量显示
 * 由任务调用，每5分钟更新一次
 */
void ui_main_update_battery_display(void);

// --- 语言设置相关 ---
// 语言类型枚举
typedef enum { LANG_ENGLISH = 0, LANG_CHINESE = 1 } ui_language_t;

/**
 * @brief 获取当前语言设置
 * @return 当前语言
 */
ui_language_t ui_get_current_language(void);

/**
 * @brief 设置语言
 * @param lang 要设置的语言
 */
void ui_set_language(ui_language_t lang);

/**
 * @brief 设置启动动画进度条百分比
 * @param percent 百分比值（0-100）
 */
void ui_start_animation_set_progress(uint8_t percent);

/**
 * @brief 更新进度条状态文本
 * @param stage 当前加载阶段
 */
void ui_start_animation_update_state(ui_load_stage_t stage);

// --- 在这里添加您未来的其他UI模块声明 ---
//
// void ui_other_screen_create(lv_obj_t* parent);
//
// ---

// --- 串口显示界面 ---
void ui_serial_display_create(lv_obj_t* parent);
void ui_serial_display_destroy(void);
void ui_serial_display_add_data(const char* data, size_t len);
void ui_serial_display_add_text(const char* text);

// --- 校准和测试界面 ---
void ui_calibration_create(lv_obj_t* parent);
void ui_calibration_destroy(void);

// --- P2P UDP图传界面 ---
void ui_image_transfer_create(lv_obj_t* parent);
void ui_image_transfer_destroy(void);

// --- 遥测界面 ---
void ui_telemetry_create(lv_obj_t* parent);
void ui_telemetry_cleanup(void);
void ui_telemetry_update_data(float voltage, float current, float roll, float pitch, float yaw, float altitude);
void ui_telemetry_update_language(void);

// 统一的back按钮创建函数
void ui_create_back_button(lv_obj_t* parent, const char* text);
void ui_create_game_back_button(lv_obj_t* parent, const char* text);

// 创建支持状态恢复的返回按钮
void ui_create_stateful_back_button(lv_obj_t* parent);

// 统一的页面标题创建函数
void ui_create_page_title(lv_obj_t* parent, const char* title_text);

// 创建页面父级容器（统一管理整个页面）
void ui_create_page_parent_container(lv_obj_t* parent, lv_obj_t** page_parent_container);

/**
 * @brief Creates a standardized top bar with a title, a back button, and an optional settings button.
 *
 * @param parent The parent object.
 * @param title_text The text to display in the title.
 * @param show_settings_btn If true, a settings button will be created on the right side.
 * @param top_bar_container Pointer to store the created top bar container.
 * @param title_container Pointer to store the created title container.
 * @param settings_btn_out Optional pointer to store the created settings button object. Can be NULL if not needed.
 */
void ui_create_top_bar(lv_obj_t* parent, const char* title_text, bool show_settings_btn, lv_obj_t** top_bar_container,
                       lv_obj_t** title_container, lv_obj_t** settings_btn_out);

// 创建页面内容容器（除开顶部栏的区域）
void ui_create_page_content_area(lv_obj_t* parent, lv_obj_t** content_container);

// Event enumeration for UI
typedef enum {
    UI_EVENT_NONE,
    UI_EVENT_WIFI_SETTINGS,
    UI_EVENT_P2P_UDP_TRANSFER,
    UI_EVENT_SERIAL_DISPLAY,
    UI_EVENT_CALIBRATION,
    UI_EVENT_TEST,
    UI_EVENT_SETTINGS_CHANGED, // Event sent when settings are changed
} ui_event_t;

extern ui_language_t g_current_language;

ui_language_t load_language_setting(void);

const lv_font_t* get_current_font(void);
const void set_language_display(lv_obj_t* obj);

#ifdef __cplusplus
}
#endif

#endif // UI_H