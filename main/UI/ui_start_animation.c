/**
 * @file ui_start_animation.c
 * @brief 高级启动动画UI实现
 * @author Your Name
 * @date 2024
 */
#include "ui.h"
#include "theme_manager.h"

typedef struct {
    const char* Initializing;
    const char* Loading_Components;
    const char* Starting_Services;
    const char* Configuring_Hardware;
    const char* Almost_Ready;
    const char* Finalizing;
} ui_animation_text_t;

ui_animation_text_t chinese_text = {
    .Initializing = "正在初始化系统",
    .Loading_Components = "正在加载组件",
    .Starting_Services = "正在启动服务",
    .Configuring_Hardware = "正在配置硬件",
    .Almost_Ready = "即将完成",
    .Finalizing = "正在收尾"
};

ui_animation_text_t english_text = {
    .Initializing = "Initializing System",
    .Loading_Components = "Loading Components",
    .Starting_Services = "Starting Services",
    .Configuring_Hardware = "Configuring Hardware",
    .Almost_Ready = "Almost Ready",
    .Finalizing = "Finalizing"
};

// 获取当前语言文本
static const ui_animation_text_t* get_current_animation_text(void) {
    return (g_current_language == LANG_CHINESE) ? &chinese_text : &english_text;
}

// 全局变量来存储回调函数和需要清理的UI元素
static ui_start_anim_finished_cb_t g_finished_cb = NULL;
static lv_obj_t* g_anim_arc = NULL;
static lv_timer_t* g_status_timer = NULL;

// 动画相关的静态函数
static void anim_logo_fade_in_cb(void* var, int32_t v) { lv_obj_set_style_opa(var, v, 0); }

static void anim_rotation_cb(void* var, int32_t v) {
    // 使用Arc自身的旋转，避免与CSS变换冲突
    lv_arc_set_rotation((lv_obj_t*)var, v / 10); // v是0.1度单位，转换为度
}

static void anim_zoom_cb(void* var, int32_t v) { lv_obj_set_style_transform_zoom(var, v, 0); }

static void anim_bar_progress_cb(void* var, int32_t v) { lv_bar_set_value(var, v, LV_ANIM_OFF); }

static void anim_status_text_timer_cb(lv_timer_t* timer) {
    lv_obj_t* label = (lv_obj_t*)timer->user_data;
    static uint32_t call_count = 0;
    call_count++;

    const ui_animation_text_t* text = get_current_animation_text();

    if (call_count <= 2) {
        lv_label_set_text(label, text->Initializing);
    } else if (call_count <= 4) {
        lv_label_set_text(label, text->Loading_Components);
    } else if (call_count <= 6) {
        lv_label_set_text(label, text->Starting_Services);
    } else if (call_count <= 8) {
        lv_label_set_text(label, text->Configuring_Hardware);
    } else if (call_count <= 10) {
        lv_label_set_text(label, text->Almost_Ready);
    } else {
        lv_label_set_text(label, text->Finalizing);
    }
    set_language_display(label);
}

static void all_anims_finished_cb(lv_anim_t* a) {
    if (g_anim_arc) {
        lv_anim_del(g_anim_arc, NULL);
        g_anim_arc = NULL;
    }
    if (g_status_timer) {
        lv_timer_del(g_status_timer);
        g_status_timer = NULL;
    }

    // 清理界面
    lv_obj_t* screen = (lv_obj_t*)a->user_data;
    if (screen) {
        lv_obj_clean(screen);
        // 应用当前主题到屏幕
        theme_apply_to_screen(screen);
    }

    // 调用外部回调函数
    if (g_finished_cb) {
        g_finished_cb();
    }
}

void ui_start_animation_create(lv_obj_t* parent, ui_start_anim_finished_cb_t finished_cb) {
    g_finished_cb = finished_cb;
    // 每次创建时重置静态变量
    g_anim_arc = NULL;
    g_status_timer = NULL;

    // 应用当前主题到屏幕
    theme_apply_to_screen(parent);

    // 1. 创建更炫酷的Logo
    lv_obj_t* logo = lv_label_create(parent);
    lv_label_set_text(logo, "ESP32-S3");
    lv_obj_set_style_text_font(logo, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(logo, lv_color_hex(0x2C2C2C), 0); // 深灰色，在浅色背景下更清晰
    lv_obj_align(logo, LV_ALIGN_CENTER, 0, -40);

    // 副标题
    lv_obj_t* subtitle = lv_label_create(parent);
    lv_label_set_text(subtitle, "Smart Terminal");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x666666), 0); // 中灰色
    lv_obj_align_to(subtitle, logo, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);

    // 2. 创建稳定的旋转光环
    lv_obj_t* arc = lv_arc_create(parent);
    g_anim_arc = arc; // 保存arc指针以便后续清理
    lv_obj_set_size(arc, 160, 160);
    lv_arc_set_rotation(arc, 0); // 设为0，通过动画旋转
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_angles(arc, 0, 90); // 设置较短的弧长，避免闪烁
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align_to(arc, logo, LV_ALIGN_CENTER, 0, 10);

    // 外层光环样式 - 莫兰迪色系
    lv_obj_set_style_arc_color(arc, lv_color_hex(0xD2CBC4), LV_PART_MAIN);      // 浅灰白
    lv_obj_set_style_arc_color(arc, lv_color_hex(0xAB9E96), LV_PART_INDICATOR); // 浅棕灰
    lv_obj_set_style_arc_width(arc, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);

    // 内层光环
    lv_obj_t* arc2 = lv_arc_create(parent);
    lv_obj_set_size(arc2, 130, 130);
    lv_arc_set_rotation(arc2, 0); // 设为0，通过动画旋转
    lv_arc_set_bg_angles(arc2, 0, 360);
    lv_arc_set_angles(arc2, 0, 60); // 更短的弧长
    lv_obj_remove_style(arc2, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc2, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align_to(arc2, logo, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_arc_color(arc2, lv_color_hex(0xC8BAAF), LV_PART_MAIN);      // 米棕灰
    lv_obj_set_style_arc_color(arc2, lv_color_hex(0xBCA79E), LV_PART_INDICATOR); // 暖棕灰
    lv_obj_set_style_arc_width(arc2, 2, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc2, 4, LV_PART_INDICATOR);

    // 3. 创建炫酷进度条
    lv_obj_t* bar = lv_bar_create(parent);
    lv_obj_set_size(bar, 220, 8);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 70);

    // 进度条样式 - 莫兰迪色系
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xD2CBC4), LV_PART_MAIN);      // 浅灰白
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xAB9E96), LV_PART_INDICATOR); // 浅棕灰
    lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);

    // 4. 创建状态文本
    lv_obj_t* status_label = lv_label_create(parent);
    if(get_current_animation_text() == &chinese_text)
        lv_label_set_text(status_label, "正在初始化系统...");
    else
        lv_label_set_text(status_label, "Initializing...");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x666666), 0); // 中灰色
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_align_to(status_label, bar, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
    set_language_display(status_label);

    // 5. 创建版本信息
    lv_obj_t* version_label = lv_label_create(parent);
    lv_label_set_text(version_label, "v2.6.2");
    lv_obj_set_style_text_color(version_label, lv_color_hex(0x999999), 0); // 浅灰色
    lv_obj_align(version_label, LV_ALIGN_BOTTOM_RIGHT, -10, -10);

    // --- 定义动画 ---
    lv_anim_t a;

    // Logo淡入动画（缩短时间）
    lv_anim_init(&a);
    lv_anim_set_var(&a, logo);
    lv_anim_set_values(&a, 0, 255);
    lv_anim_set_time(&a, 500); // 缩短到0.5秒
    lv_anim_set_delay(&a, 100);
    lv_anim_set_exec_cb(&a, anim_logo_fade_in_cb);
    lv_anim_start(&a);

    // 副标题淡入动画
    lv_anim_init(&a);
    lv_anim_set_var(&a, subtitle);
    lv_anim_set_values(&a, 0, 255);
    lv_anim_set_time(&a, 600);
    lv_anim_set_delay(&a, 300);
    lv_anim_set_exec_cb(&a, anim_logo_fade_in_cb);
    lv_anim_start(&a);

    // --- 外层光环动画（平滑旋转）---
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc);
    lv_anim_set_values(&a, 0, 3600); // 旋转360度 (LVGL角度单位是0.1度)
    lv_anim_set_time(&a, 5000);      // 5秒一圈，更平滑
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_linear); // 线性动画，避免加速减速
    lv_anim_set_exec_cb(&a, anim_rotation_cb);
    lv_anim_start(&a);

    // --- 内层光环动画（反向旋转）---
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc2);
    lv_anim_set_values(&a, 3600, 0); // 反向旋转
    lv_anim_set_time(&a, 3000);      // 4秒一圈
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_linear); // 线性动画
    lv_anim_set_exec_cb(&a, anim_rotation_cb);
    lv_anim_start(&a);

    // 进度条动画（缩短时间）
    lv_anim_init(&a);
    lv_anim_set_var(&a, bar);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_time(&a, 2000); // 缩短到2秒
    lv_anim_set_delay(&a, 200); // 延迟0.2秒开始
    lv_anim_set_exec_cb(&a, anim_bar_progress_cb);
    lv_anim_set_ready_cb(&a, all_anims_finished_cb); // 最后一个动画完成时调用总回调
    lv_anim_set_user_data(&a, parent);
    lv_anim_start(&a);

    // 状态文本更新定时器（更新更频繁的状态）
    g_status_timer = lv_timer_create(anim_status_text_timer_cb, 400, status_label);
}