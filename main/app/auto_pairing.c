/**
 * @file auto_pairing.c
 * @brief 自动配对功能实现
 * @author TidyCraze
 * @date 2025-01-15
 */

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "theme_manager.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

static const char* TAG = "AUTO_PAIRING";

// 配对状态
typedef enum {
    PAIRING_STATE_IDLE,
    PAIRING_STATE_WAITING,
    PAIRING_STATE_CONNECTING,
    PAIRING_STATE_SUCCESS,
    PAIRING_STATE_FAILED,
    PAIRING_STATE_RESTARTING
} pairing_state_t;

// 全局变量
static pairing_state_t g_pairing_state = PAIRING_STATE_IDLE;
static lv_obj_t* g_pairing_window = NULL;
static lv_obj_t* g_status_label = NULL;
static lv_obj_t* g_loading_arc = NULL;
static lv_timer_t* g_pairing_timer = NULL;
static esp_timer_handle_t g_countdown_timer = NULL;
static uint32_t g_remaining_time = 180; // 3分钟 = 180秒

// AP配置
#define PAIRING_AP_SSID_PREFIX "DisplayTerminal_"
#define PAIRING_AP_PASSWORD "12345678"
#define PAIRING_AP_CHANNEL 6

// 事件组
static EventGroupHandle_t g_pairing_event_group = NULL;
#define PAIRING_SUCCESS_BIT BIT0
#define PAIRING_TIMEOUT_BIT BIT1
#define PAIRING_CANCEL_BIT BIT2

// 前向声明
static void create_pairing_window(void);
static void destroy_pairing_window(void);
static void update_pairing_status(const char* status_text);
static void pairing_timer_cb(lv_timer_t* timer);
static void countdown_timer_cb(void* arg);
static void start_ap_hotspot(void);
static void stop_ap_hotspot(void);
static void send_tcp_command(void);
static bool simulate_device_connection(void);

/**
 * @brief 启动自动配对功能
 */
void auto_pairing_start(void) {
    ESP_LOGI(TAG, "Starting auto pairing");

    if (g_pairing_state != PAIRING_STATE_IDLE) {
        ESP_LOGW(TAG, "Pairing already in progress");
        return;
    }

    // 创建事件组
    if (g_pairing_event_group == NULL) {
        g_pairing_event_group = xEventGroupCreate();
    }

    // 重置状态
    g_pairing_state = PAIRING_STATE_WAITING;
    g_remaining_time = 180;

    // 创建配对窗口
    create_pairing_window();

    // 启动倒计时定时器
    const esp_timer_create_args_t countdown_timer_args = {.callback = countdown_timer_cb,
                                                          .name = "pairing_countdown"};
    esp_timer_create(&countdown_timer_args, &g_countdown_timer);
    esp_timer_start_periodic(g_countdown_timer, 1000000); // 1秒间隔

    // 启动AP热点
    start_ap_hotspot();

    ESP_LOGI(TAG, "Auto pairing started, 3 minutes countdown begin");
}

/**
 * @brief 停止自动配对功能
 */
void auto_pairing_stop(void) {
    ESP_LOGI(TAG, "Stopping auto pairing");

    // 停止定时器
    if (g_countdown_timer) {
        esp_timer_stop(g_countdown_timer);
        esp_timer_delete(g_countdown_timer);
        g_countdown_timer = NULL;
    }

    if (g_pairing_timer) {
        lv_timer_del(g_pairing_timer);
        g_pairing_timer = NULL;
    }

    // 停止AP热点
    stop_ap_hotspot();

    // 销毁窗口
    destroy_pairing_window();

    // 重置状态
    g_pairing_state = PAIRING_STATE_IDLE;

    // 删除事件组
    if (g_pairing_event_group) {
        vEventGroupDelete(g_pairing_event_group);
        g_pairing_event_group = NULL;
    }

    ESP_LOGI(TAG, "Auto pairing stopped");
}

/**
 * @brief 创建配对窗口
 */
static void create_pairing_window(void) {
    // 创建全屏覆盖窗口
    g_pairing_window = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_pairing_window, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(g_pairing_window, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_pairing_window, LV_OPA_80, 0);

    // 创建内容容器
    lv_obj_t* container = lv_obj_create(g_pairing_window);
    lv_obj_set_size(container, 250, 200);
    lv_obj_center(container);
    lv_obj_set_style_bg_color(container, lv_color_white(), 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_shadow_width(container, 20, 0);
    lv_obj_set_style_shadow_color(container, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(container, LV_OPA_50, 0);
    lv_obj_set_style_radius(container, 10, 0);

    // 创建旋转加载动画
    g_loading_arc = lv_arc_create(container);
    lv_obj_set_size(g_loading_arc, 60, 60);
    lv_arc_set_range(g_loading_arc, 0, 360);
    lv_arc_set_value(g_loading_arc, 0);
    lv_arc_set_bg_angles(g_loading_arc, 0, 360);
    lv_obj_set_style_arc_width(g_loading_arc, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(g_loading_arc, 6, LV_PART_INDICATOR);
    lv_obj_align(g_loading_arc, LV_ALIGN_TOP_MID, 0, 30);

    // 创建状态标签
    g_status_label = lv_label_create(container);
    if (ui_get_current_language() == LANG_CHINESE) {
        lv_label_set_text(g_status_label, "正在初始化...");
    } else {
        lv_label_set_text(g_status_label, "Initializing...");
    }
    lv_obj_align(g_status_label, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_set_style_text_font(g_status_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(g_status_label, lv_color_black(), 0);

    // 创建倒计时标签
    lv_obj_t* countdown_label = lv_label_create(container);
    lv_label_set_text_fmt(countdown_label, "03:00");
    lv_obj_align(countdown_label, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_text_font(countdown_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(countdown_label, lv_color_hex(0x666666), 0);

    // 启动LVGL定时器来旋转加载动画
    g_pairing_timer = lv_timer_create(pairing_timer_cb, 50, countdown_label);
}

/**
 * @brief 销毁配对窗口
 */
static void destroy_pairing_window(void) {
    if (g_pairing_window) {
        lv_obj_del(g_pairing_window);
        g_pairing_window = NULL;
        g_status_label = NULL;
        g_loading_arc = NULL;
    }
}

/**
 * @brief 更新配对状态显示
 */
static void update_pairing_status(const char* status_text) {
    if (g_status_label) {
        lv_label_set_text(g_status_label, status_text);
    }
    ESP_LOGI(TAG, "Pairing status: %s", status_text);
}

/**
 * @brief LVGL定时器回调 - 旋转加载动画
 */
static void pairing_timer_cb(lv_timer_t* timer) {
    lv_obj_t* countdown_label = (lv_obj_t*)timer->user_data;

    // 旋转加载动画
    static int angle = 0;
    angle = (angle + 10) % 360;
    lv_arc_set_value(g_loading_arc, angle);

    // 更新倒计时显示
    if (countdown_label && g_remaining_time > 0) {
        uint32_t minutes = g_remaining_time / 60;
        uint32_t seconds = g_remaining_time % 60;
        lv_label_set_text_fmt(countdown_label, "%02lu:%02lu", minutes, seconds);
    }
}

/**
 * @brief 倒计时定时器回调
 */
static void countdown_timer_cb(void* arg) {
    if (g_remaining_time > 0) {
        g_remaining_time--;

        // 模拟设备连接检测 (在倒计时进行到一半时模拟连接成功)
        if (g_remaining_time == 90 && simulate_device_connection()) {
            // 设备连接成功
            g_pairing_state = PAIRING_STATE_SUCCESS;

            // 停止倒计时定时器
            if (g_countdown_timer) {
                esp_timer_stop(g_countdown_timer);
            }

            // 显示重启提示（双语）
            extern ui_language_t ui_get_current_language(void);
            if (ui_get_current_language() == LANG_CHINESE) {
                update_pairing_status("重启接收端中，请等待绿灯呼吸闪烁");
            } else {
                update_pairing_status(
                    "Restarting receiver. Please wait for the green light to breathe");
            }

            // 3秒后显示成功提示
            vTaskDelay(pdMS_TO_TICKS(3000));

            // 发送TCP命令
            send_tcp_command();

            // 显示成功提示（双语）
            if (ui_get_current_language() == LANG_CHINESE) {
                update_pairing_status("配对成功");
            } else {
                update_pairing_status("Pairing successful!");
            }

            // 2秒后停止配对
            vTaskDelay(pdMS_TO_TICKS(2000));
            auto_pairing_stop();

            return;
        }
    } else {
        // 倒计时结束，配对失败
        g_pairing_state = PAIRING_STATE_FAILED;
        if (ui_get_current_language() == LANG_CHINESE) {
            update_pairing_status("配对失败 - 超时");
        } else {
            update_pairing_status("Pairing failed - timeout");
        }

        // 2秒后停止配对
        vTaskDelay(pdMS_TO_TICKS(2000));
        auto_pairing_stop();
    }
}

/**
 * @brief 启动AP热点
 */
static void start_ap_hotspot(void) {
    ESP_LOGI(TAG, "Starting AP hotspot for pairing");

    // 获取MAC地址后四位作为SSID后缀
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "%s%02X%02X", PAIRING_AP_SSID_PREFIX, mac[4], mac[5]);

    // 配置AP
    wifi_config_t wifi_config = {.ap = {.ssid_len = strlen(ssid),
                                        .channel = PAIRING_AP_CHANNEL,
                                        .password = PAIRING_AP_PASSWORD,
                                        .max_connection = 1,
                                        .authmode = WIFI_AUTH_WPA2_PSK}};
    strcpy((char*)wifi_config.ap.ssid, ssid);

    // 设置WiFi模式为AP
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP hotspot started: %s", ssid);
    if (ui_get_current_language() == LANG_CHINESE) {
        update_pairing_status("正在等待设备连接...");
    } else {
        update_pairing_status("Waiting for device connection...");
    }
}

/**
 * @brief 停止AP热点
 */
static void stop_ap_hotspot(void) {
    ESP_LOGI(TAG, "Stopping AP hotspot");
    ESP_ERROR_CHECK(esp_wifi_stop());
}

/**
 * @brief 模拟设备连接检测
 */
static bool simulate_device_connection(void) {
    // 这里可以实现真实的连接检测逻辑
    // 目前使用随机模拟，在50%的概率下"检测到"连接
    bool connected = (esp_random() % 2) == 0;

    if (connected) {
        ESP_LOGI(TAG, "Simulated device connection detected");
    } else {
        ESP_LOGI(TAG, "No device connection detected");
    }

    return connected;
}

/**
 * @brief 发送TCP命令
 */
static void send_tcp_command(void) {
    ESP_LOGI(TAG, "TCP command sent to receiver.");
    // 这里可以实现真实的TCP通信逻辑
    // 目前只打印日志信息
}

/**
 * @brief 获取配对状态
 */
pairing_state_t auto_pairing_get_state(void) { return g_pairing_state; }

/**
 * @brief 检查配对是否正在进行
 */
bool auto_pairing_is_active(void) { return g_pairing_state != PAIRING_STATE_IDLE; }
