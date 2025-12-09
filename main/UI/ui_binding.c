/**
 * @file ui_binding.c
 * @brief 配对模式UI界面 - 完整的ELRS配对集成
 * @author TidyCraze
 * @date 2025-12-09
 */

#include "esp_log.h"
#include "ui.h"
#include "ui_common.h"
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

// ELRS SDK函数声明
extern void EnterBindingModeSafely(void);
extern void ExitBindingMode_Public(void);
extern bool InBindingMode;  // 全局变量，来自tx_main.cpp
// 注意: SetSyncSpam() 在C++中定义，不能直接从C代码调用，但这个函数只是处理配置修改标志
// 绑定模式不需要它，直接跳过即可

// UI通用函数声明
extern const lv_font_t* get_current_font(void);
extern void set_language_display(lv_obj_t* obj);
extern void theme_apply_to_label(lv_obj_t* label, bool is_title);

// 前向声明主菜单创建函数
void ui_main_menu_create(lv_obj_t* parent);

static const char* TAG = "UI_BINDING";

// 语言文本定义
typedef struct {
    const char* title;
    const char* status_label;
    const char* status_waiting;
    const char* status_binding;
    const char* status_success;
    const char* status_timeout;
    const char* hint_text;
    const char* hint_put_rx;
    const char* hint_binding_active;
    const char* hint_waiting_rx;
    const char* cancel_button;
    const char* back_button;
} binding_text_t;

// 英文文本
static const binding_text_t binding_english_text = {
    .title = "Binding Mode",
    .status_label = "Status",
    .status_waiting = "Ready to bind",
    .status_binding = "Binding in progress...",
    .status_success = "Binding successful!",
    .status_timeout = "Binding timeout - no receiver found",
    .hint_text = "Instructions",
    .hint_put_rx = "1. Put receiver in binding mode",
    .hint_binding_active = "2. Binding mode is active",
    .hint_waiting_rx = "3. Waiting for receiver response...",
    .cancel_button = "Cancel",
    .back_button = "Back",
};

// 中文文本
static const binding_text_t binding_chinese_text = {
    .title = "配对模式",
    .status_label = "状态",
    .status_waiting = "准备配对",
    .status_binding = "正在配对...",
    .status_success = "配对成功！",
    .status_timeout = "配对超时 - 未找到接收器",
    .hint_text = "操作说明",
    .hint_put_rx = "1. 将接收器置于配对模式",
    .hint_binding_active = "2. 配对模式已激活",
    .hint_waiting_rx = "3. 等待接收器响应",
    .cancel_button = "取消",
    .back_button = "返回",
};

// 配对页面状态结构体
typedef struct {
    lv_obj_t* screen;
    lv_obj_t* main_container;
    lv_obj_t* status_label;
    lv_obj_t* status_value_label;
    lv_obj_t* hint_container;
    lv_obj_t* hint_label;
    lv_obj_t* progress_bar;
    lv_obj_t* start_button;
    lv_obj_t* cancel_button;
    lv_obj_t* back_button;
    lv_timer_t* status_update_timer;
    uint32_t binding_start_time;
    bool binding_active;
    uint8_t binding_timeout_sec;
} binding_ui_state_t;

static binding_ui_state_t g_binding_state = {0};

// 互斥锁用于保护ELRS操作
static SemaphoreHandle_t g_binding_mutex = NULL;

// 标志用于启动绑定任务
static volatile bool g_binding_start_requested = false;
static volatile bool g_binding_monitor_running = false;
static TaskHandle_t g_binding_monitor_handle = NULL;

/**
 * @brief 持续运行的绑定监控任务 - 完全独立于UI事件循环
 * 在应用启动时创建一次，然后持续监控binding请求
 */
static void binding_monitor_task(void* arg) {
    ESP_LOGI(TAG, "\n=== BINDING MONITOR TASK STARTED ===");
    ESP_LOGI(TAG, "This task will continuously monitor for binding requests");
    g_binding_monitor_running = true;
    
    while (1) {
        // 检查是否收到绑定请求
        if (g_binding_start_requested) {
            ESP_LOGI(TAG, "\n>>> BINDING REQUEST RECEIVED <<<");
            g_binding_start_requested = false;
            
            // 等待系统稳定
            ESP_LOGI(TAG, "→ Waiting 2 seconds for system to stabilize...");
            vTaskDelay(2000 / portTICK_PERIOD_MS);
            
            // 记录进入绑定模式前的状态
            ESP_LOGI(TAG, "→ InBindingMode before: %d", InBindingMode);
            ESP_LOGI(TAG, "→ Calling EnterBindingModeSafely()");
            
            // 调用ELRS绑定函数
            EnterBindingModeSafely();
            
            // 确认返回
            ESP_LOGI(TAG, "EnterBindingModeSafely() returned");
            ESP_LOGI(TAG, "InBindingMode after: %d", InBindingMode);
            
            // 等待绑定完成
            uint64_t binding_start = esp_timer_get_time();
            uint64_t binding_timeout_us = 60000000;
            int last_log_sec = 0;
            
            ESP_LOGI(TAG, "Waiting for binding to complete (timeout: %lu us)...", binding_timeout_us);
            
            while (InBindingMode && ((esp_timer_get_time() - binding_start) < binding_timeout_us)) {
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                uint32_t elapsed_sec = (esp_timer_get_time() - binding_start) / 1000000;
                if (elapsed_sec != last_log_sec && elapsed_sec % 5 == 0) {
                    ESP_LOGI(TAG, "   ... binding in progress (%lu seconds)", elapsed_sec);
                    last_log_sec = elapsed_sec;
                }
            }
            
            uint32_t total_time = (esp_timer_get_time() - binding_start) / 1000;
            if (InBindingMode) {
                ESP_LOGW(TAG, "Binding timed out after %lu ms", total_time);
            } else {
                ESP_LOGI(TAG, "Binding completed successfully (%lu ms)", total_time);
            }
        }
        
        // 小延迟，防止忙轮询
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

/**
 * @brief 获取当前语言的文本
 */
static const binding_text_t* get_binding_text(void) {
    return ui_get_current_language() == LANG_CHINESE ? &binding_chinese_text : &binding_english_text;
}

/**
 * @brief 返回按钮回调函数
 */
static void binding_back_button_cb(lv_event_t* e) {
    ESP_LOGI(TAG, "Back button pressed, returning to main menu");
    
    // 停止配对计时器
    if (g_binding_state.status_update_timer != NULL) {
        lv_timer_del(g_binding_state.status_update_timer);
        g_binding_state.status_update_timer = NULL;
    }
    
    // 清空绑定状态
    g_binding_state.binding_active = false;
    
    // 退出SDK的配对模式
    if (InBindingMode) {
        ExitBindingMode_Public();
    }
    
    // 清除当前屏幕并返回主菜单
    lv_obj_clean(lv_scr_act());
    ui_main_menu_create(lv_scr_act());
}

/**
 * @brief 启动配对按钮回调函数
 * 只负责设置标志，绝不调用ELRS函数或做任何耗时操作
 */
static void binding_start_button_cb(lv_event_t* e) {
    ESP_LOGI(TAG, "✓ Binding start button pressed");
    
    // 仅设置标志，让后台监控任务负责真正的绑定工作
    // 这样可以避免在LVGL事件上下文中调用ELRS函数
    g_binding_start_requested = true;
    
    ESP_LOGI(TAG, "  → Binding monitor task will handle the request");
}

/**
 * @brief 取消配对按钮回调函数
 */
static void binding_cancel_button_cb(lv_event_t* e) {
    ESP_LOGI(TAG, "Cancel binding button pressed");
    
    // 停止配对计时器
    if (g_binding_state.status_update_timer != NULL) {
        lv_timer_del(g_binding_state.status_update_timer);
        g_binding_state.status_update_timer = NULL;
    }
    
    // 清空绑定状态
    g_binding_state.binding_active = false;
    
    // 退出SDK的配对模式
    if (InBindingMode) {
        ExitBindingMode_Public();
    }
    
    // 隐藏进度条
    if (g_binding_state.progress_bar) {
        lv_obj_add_flag(g_binding_state.progress_bar, LV_OBJ_FLAG_HIDDEN);
    }
    
    // 更新UI显示为取消状态
    if (g_binding_state.status_value_label) {
        lv_label_set_text(g_binding_state.status_value_label, "Cancelled");
        lv_obj_set_style_text_color(g_binding_state.status_value_label, 
                                   lv_color_hex(0xFF6B6B), 0);  // 红色表示已取消
    }
    
    if (g_binding_state.hint_label) {
        lv_label_set_text(g_binding_state.hint_label, "Instructions");
    }
    
    // 启用启动按钮
    if (g_binding_state.start_button) {
        lv_obj_clear_state(g_binding_state.start_button, LV_STATE_DISABLED);
    }
    
    ESP_LOGI(TAG, "Binding cancelled");
}

/**
 * @brief 状态更新定时器回调 - 最小化以避免所有线程安全问题
 */
static void binding_status_timer_cb(lv_timer_t* timer) {
    if (!g_binding_state.binding_active) {
        // 如果绑定已停止，删除定时器
        if (g_binding_state.status_update_timer) {
            lv_timer_del(g_binding_state.status_update_timer);
            g_binding_state.status_update_timer = NULL;
        }
        return;
    }
}

/**
 * @brief 开始配对模式 - 最小化操作以避免LVGL冲突
 */
void ui_binding_start(void) {
    ESP_LOGI(TAG, "Starting binding mode UI update");
    
    // 只设置标志，不做任何LVGL操作
    // LVGL操作可能与ELRS硬件中断冲突
    g_binding_state.binding_active = true;
    g_binding_state.binding_start_time = lv_tick_get();
    g_binding_state.binding_timeout_sec = 30;
    
    // 简单的UI提示 - 不调用LVGL函数
    ESP_LOGI(TAG, "Binding in progress - waiting for receiver...");
}

/**
 * @brief 设置配对成功状态
 */
void ui_binding_success(void) {
    ESP_LOGI(TAG, "Binding successful");
    
    g_binding_state.binding_active = false;
    if (g_binding_state.status_update_timer != NULL) {
        lv_timer_del(g_binding_state.status_update_timer);
        g_binding_state.status_update_timer = NULL;
    }
    
    if (g_binding_state.status_value_label) {
        lv_obj_set_style_text_color(g_binding_state.status_value_label, 
                                   lv_color_hex(0x00FF00), 0);  // 绿色表示成功
        lv_label_set_text(g_binding_state.status_value_label, "Binding successful!");
    }
    
    if (g_binding_state.hint_label) {
        lv_label_set_text(g_binding_state.hint_label, "Instructions");
    }
    
    // 隐藏进度条
    if (g_binding_state.progress_bar) {
        lv_obj_add_flag(g_binding_state.progress_bar, LV_OBJ_FLAG_HIDDEN);
    }
    
    // 显示启动按钮，隐藏取消按钮
    if (g_binding_state.start_button) {
        lv_obj_clear_state(g_binding_state.start_button, LV_STATE_DISABLED);
        lv_obj_clear_flag(g_binding_state.start_button, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_binding_state.cancel_button) {
        lv_obj_add_flag(g_binding_state.cancel_button, LV_OBJ_FLAG_HIDDEN);
    }
}
    
/**
 * @brief 创建配对界面
 */
void ui_binding_create(lv_obj_t* parent) {
    if (!parent) {
        ESP_LOGE(TAG, "Invalid parent for binding UI");
        return;
    }

    ESP_LOGI(TAG, "Creating binding UI");

    // 清空内存结构
    memset(&g_binding_state, 0, sizeof(binding_ui_state_t));

    // 创建主容器
    g_binding_state.main_container = lv_obj_create(parent);
    lv_obj_set_size(g_binding_state.main_container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(g_binding_state.main_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(g_binding_state.main_container, 16, 0);
    lv_obj_set_style_pad_gap(g_binding_state.main_container, 16, 0);

    // 标题
    lv_obj_t* title = lv_label_create(g_binding_state.main_container);
    lv_label_set_text(title, "Binding Mode");
    lv_obj_set_style_text_font(title, get_current_font(), 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    set_language_display(title);

    // 分隔线
    lv_obj_t* divider = lv_obj_create(g_binding_state.main_container);
    lv_obj_set_size(divider, LV_PCT(100), 2);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0x444444), 0);

    // 状态容器
    lv_obj_t* status_container = lv_obj_create(g_binding_state.main_container);
    lv_obj_set_size(status_container, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(status_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(status_container, 12, 0);
    lv_obj_set_style_pad_gap(status_container, 8, 0);
    lv_obj_set_style_bg_color(status_container, lv_color_hex(0x222222), 0);

    // 状态标签
    g_binding_state.status_label = lv_label_create(status_container);
    lv_label_set_text(g_binding_state.status_label, "Status");
    lv_obj_set_style_text_color(g_binding_state.status_label, lv_color_hex(0xCCCCCC), 0);
    theme_apply_to_label(g_binding_state.status_label, false);
    set_language_display(g_binding_state.status_label);

    // 状态值标签
    g_binding_state.status_value_label = lv_label_create(status_container);
    lv_label_set_text(g_binding_state.status_value_label, "Waiting...");
    lv_obj_set_style_text_font(g_binding_state.status_value_label, get_current_font(), 0);
    lv_obj_set_style_text_color(g_binding_state.status_value_label, lv_color_hex(0x00FF00), 0);
    set_language_display(g_binding_state.status_value_label);

    // 提示容器
    lv_obj_t* hint_title = lv_label_create(g_binding_state.main_container);
    lv_label_set_text(hint_title, "Instructions");
    lv_obj_set_style_text_color(hint_title, lv_color_hex(0xFFCC00), 0);
    theme_apply_to_label(hint_title, false);
    set_language_display(hint_title);

    // 提示内容标签（支持多行文本）
    g_binding_state.hint_label = lv_label_create(g_binding_state.main_container);
    // 使用静态缓冲区构建提示文本
    static char hint_text_buf[256];
    snprintf(hint_text_buf, sizeof(hint_text_buf), "%s\n%s\n%s",
             "1. Put receiver in binding mode", "2. Binding mode is active", "3. Waiting for receiver response...");
    lv_label_set_text(g_binding_state.hint_label, hint_text_buf);
    lv_obj_set_style_text_color(g_binding_state.hint_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_width(g_binding_state.hint_label, LV_PCT(100));
    lv_label_set_long_mode(g_binding_state.hint_label, LV_LABEL_LONG_WRAP);
    theme_apply_to_label(g_binding_state.hint_label, false);
    set_language_display(g_binding_state.hint_label);

    // 进度条
    g_binding_state.progress_bar = lv_bar_create(g_binding_state.main_container);
    lv_obj_set_size(g_binding_state.progress_bar, LV_PCT(100), 20);
    lv_bar_set_range(g_binding_state.progress_bar, 0, 100);
    lv_bar_set_value(g_binding_state.progress_bar, 100, LV_ANIM_OFF);
    lv_obj_add_flag(g_binding_state.progress_bar, LV_OBJ_FLAG_HIDDEN);  // 初始隐藏
    
    // 进度条样式
    lv_obj_set_style_bg_color(g_binding_state.progress_bar, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(g_binding_state.progress_bar, lv_color_hex(0x0088FF), 
                             LV_PART_INDICATOR);

    // 按钮容器
    lv_obj_t* button_container = lv_obj_create(g_binding_state.main_container);
    lv_obj_set_size(button_container, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(button_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button_container, LV_FLEX_ALIGN_SPACE_AROUND, 
                         LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(button_container, 12, 0);

    // 启动按钮
    g_binding_state.start_button = lv_btn_create(button_container);
    lv_obj_set_width(g_binding_state.start_button, LV_PCT(45));
    lv_obj_set_height(g_binding_state.start_button, 45);
    lv_obj_add_event_cb(g_binding_state.start_button, binding_start_button_cb, 
                       LV_EVENT_CLICKED, NULL);
    
    lv_obj_t* start_label = lv_label_create(g_binding_state.start_button);
    lv_label_set_text(start_label, "Start");
    lv_obj_set_style_text_font(start_label, get_current_font(), 0);
    lv_obj_center(start_label);
    set_language_display(start_label);

    // 取消按钮
    g_binding_state.cancel_button = lv_btn_create(button_container);
    lv_obj_set_width(g_binding_state.cancel_button, LV_PCT(45));
    lv_obj_set_height(g_binding_state.cancel_button, 45);
    lv_obj_add_event_cb(g_binding_state.cancel_button, binding_cancel_button_cb, 
                       LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(g_binding_state.cancel_button, LV_OBJ_FLAG_HIDDEN);  // 初始隐藏
    
    lv_obj_t* cancel_label = lv_label_create(g_binding_state.cancel_button);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_set_style_text_font(cancel_label, get_current_font(), 0);
    lv_obj_center(cancel_label);
    set_language_display(cancel_label);

    // 返回按钮
    g_binding_state.back_button = lv_btn_create(button_container);
    lv_obj_set_width(g_binding_state.back_button, LV_PCT(45));
    lv_obj_set_height(g_binding_state.back_button, 45);
    lv_obj_add_event_cb(g_binding_state.back_button, binding_back_button_cb, 
                       LV_EVENT_CLICKED, NULL);
    
    lv_obj_t* back_label = lv_label_create(g_binding_state.back_button);
    lv_label_set_text(back_label, "Back");
    lv_obj_set_style_text_font(back_label, get_current_font(), 0);
    lv_obj_center(back_label);
    set_language_display(back_label);

    ESP_LOGI(TAG, "Binding UI created successfully");
}

/**
 * @brief 销毁配对界面
 */
void ui_binding_destroy(void) {
    if (g_binding_state.status_update_timer != NULL) {
        lv_timer_del(g_binding_state.status_update_timer);
        g_binding_state.status_update_timer = NULL;
    }
    
    g_binding_state.binding_active = false;
    memset(&g_binding_state, 0, sizeof(binding_ui_state_t));
    
    ESP_LOGI(TAG, "Binding UI destroyed");
}

/**
 * @brief 检查配对模式是否处于活跃状态
 */
bool ui_binding_is_active(void) {
    return g_binding_state.binding_active && InBindingMode;
}

/**
 * @brief 从外部事件触发配对完成
 */
void ui_binding_notify_success(void) {
    if (ui_binding_is_active()) {
        ui_binding_success();
    }
}

/**
 * @brief 从外部事件触发配对失败
 */
void ui_binding_notify_failure(void) {
    if (ui_binding_is_active()) {
        ESP_LOGW(TAG, "Binding failed from external notification");
        
        g_binding_state.binding_active = false;
        
        if (g_binding_state.status_update_timer != NULL) {
            lv_timer_del(g_binding_state.status_update_timer);
            g_binding_state.status_update_timer = NULL;
        }
        
        if (g_binding_state.status_value_label) {
            lv_obj_set_style_text_color(g_binding_state.status_value_label, 
                                       lv_color_hex(0xFF6B6B), 0);
            lv_label_set_text(g_binding_state.status_value_label, "Binding timeout");
        }
        
        if (g_binding_state.hint_label) {
            lv_label_set_text(g_binding_state.hint_label, "Instructions");
        }
        
        if (g_binding_state.progress_bar) {
            lv_obj_add_flag(g_binding_state.progress_bar, LV_OBJ_FLAG_HIDDEN);
        }
        
        if (g_binding_state.start_button) {
            lv_obj_clear_state(g_binding_state.start_button, LV_STATE_DISABLED);
            lv_obj_clear_flag(g_binding_state.start_button, LV_OBJ_FLAG_HIDDEN);
        }
        
        if (g_binding_state.cancel_button) {
            lv_obj_add_flag(g_binding_state.cancel_button, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/**
 * @brief 初始化UI配对模块
 */
void ui_binding_module_init(void) {
    memset(&g_binding_state, 0, sizeof(binding_ui_state_t));
    ESP_LOGI(TAG, "Binding module initialized");
    
    // 创建持续运行的绑定监控任务
    // 这个任务会在应用运行期间持续监控绑定请求
    ESP_LOGI(TAG, "Creating binding monitor task...");
    BaseType_t ret = xTaskCreate(
        binding_monitor_task,           // 任务函数
        "binding_monitor",              // 任务名称
        4096,                           // 堆栈大小
        NULL,                           // 参数
        tskIDLE_PRIORITY + 2,           // 优先级（低于UI线程但高于空闲）
        &g_binding_monitor_handle       // 任务句柄
    );
    
    if (ret == pdPASS) {
        ESP_LOGI(TAG, "Binding monitor task created successfully");
    } else {
        ESP_LOGE(TAG, "Failed to create binding monitor task!");
    }
}
