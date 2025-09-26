/**
 * @file lvgl_main.c
 * @brief LVGL主任务，负责UI的初始化和流程控制
 * @author Your Name
 * @date 2025-08-14
 */
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "my_font.h"
#include "settings_manager.h"
#include "theme_manager.h"
#include "ui.h"
#include "st7789.h"


// 动画完成后的回调函数
static void show_main_menu_cb(void) { ui_main_menu_create(lv_scr_act()); }

static void lv_tick_task(void* arg) {
    (void)arg;
    lv_tick_inc(10);
}

void lvgl_main_task(void* pvParameters) {

    const char* TAG = "LVGL_DEMO";
    ESP_LOGI(TAG, "LVGL task started on core %d", xPortGetCoreID());

    lv_init();   // 初始化LVGL
    font_init(); // 初始化字体
    lv_port_disp_init();
    lv_port_indev_init();

    // 在屏幕硬件和设置都初始化完成后，应用背光
    st7789_set_backlight(settings_get_backlight());
    // 初始化主题管理器
    theme_manager_init();

    // 应用当前主题到默认屏幕
    theme_apply_to_screen(lv_scr_act());

    esp_timer_handle_t periodic_timer;
    const esp_timer_create_args_t periodic_timer_args = {.callback = &lv_tick_task, .name = "lv_tick"};

    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 16666)); // 16.666ms周期
    ESP_LOGI(TAG, "LVGL tick timer started (10ms period)");

    // 创建并启动开机动画，动画结束后调用 show_main_menu_cb
    ui_start_animation_create(lv_scr_act(), show_main_menu_cb);

    ESP_LOGI(TAG, "LVGL UI flow started with animation");

    // LVGL主循环 - 专用任务处理
    while (1) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(16)); // 60Hz刷新率
    }
}