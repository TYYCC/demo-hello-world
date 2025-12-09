#ifndef UI_H
#define UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "theme_manager.h"
#include "ui_state_manager.h"

// Define loading stage enum
typedef enum {
    UI_STAGE_INITIALIZING = 0,// Initializing stage
    UI_STAGE_LOADING_COMPONENTS,// Loading components stage
    UI_STAGE_STARTING_SERVICES,// Starting services stage
    UI_STAGE_CONFIGURING_HARDWARE,// Configuring hardware stage
    UI_STAGE_ALMOST_READY,// Almost ready stage
    UI_STAGE_FINALIZING,// Finalizing stage
    UI_STAGE_DONE
} ui_load_stage_t;

// --- LVGL main task ---
// This task initializes and runs the LVGL main loop
void lvgl_main_task(void* pvParameters);

// --- Start animation UI ---

/**
 * @brief Callback function type when start animation finishes
 */
typedef void (*ui_start_anim_finished_cb_t)(void);

/**
 * @brief Create and start playing start animation
 * @param parent    Parent object, usually lv_scr_act()
 * @param finished_cb Callback function called when animation finishes
 */
void ui_start_animation_create(lv_obj_t* parent, ui_start_anim_finished_cb_t finished_cb);

/**
 * @brief Create main menu interface
 * @param parent Parent object, usually lv_scr_act()
 */
void ui_main_menu_create(lv_obj_t* parent);

/**
 * @brief Create WiFi settings interface
 * @param parent Parent object, usually lv_scr_act()
 */
void ui_wifi_settings_create(lv_obj_t* parent);

/**
 * @brief Create system settings interface
 * @param parent Parent object, usually lv_scr_act()
 */
void ui_settings_create(lv_obj_t* parent);

/**
 * @brief Create pairing interface
 * @param parent 父对象，通常是 lv_scr_act()
 */
void ui_binding_create(lv_obj_t* parent);

/**
 * @brief 销毁配对界面
 */
void ui_binding_destroy(void);

/**
 * @brief 开始配对模式
 */
void ui_binding_start(void);

/**
 * @brief 设置配对成功状态
 */
void ui_binding_success(void);

/**
 * @brief 初始化UI配对模块
 */
void ui_binding_module_init(void);

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