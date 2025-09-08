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
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "lwip/sockets.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "theme_manager.h"
#include "ui.h"
#include "my_font.h"

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
static lv_obj_t* g_loading_spinner = NULL;
static lv_obj_t* g_countdown_label = NULL;
static lv_timer_t* g_pairing_timer = NULL;
static esp_timer_handle_t g_countdown_timer = NULL;
static uint32_t g_remaining_time = 60;
static esp_ip4_addr_t g_client_ip = {0};

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
static void pairing_window_close_cb(lv_timer_t* timer);
static void countdown_timer_cb(void* arg);
static void pairing_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void start_ap_hotspot(void);
static void stop_ap_hotspot(void);
static void send_tcp_command(void);
static bool is_device_connected(void);


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
    g_remaining_time = 60;

    // 创建配对窗口
    create_pairing_window();

    // 启动倒计时定时器
    const esp_timer_create_args_t countdown_timer_args = {.callback = countdown_timer_cb,
                                                          .name = "pairing_countdown"};
    esp_timer_create(&countdown_timer_args, &g_countdown_timer);
    esp_timer_start_periodic(g_countdown_timer, 1000000); // 1秒间隔

    // 启动AP热点
    start_ap_hotspot();

    ESP_LOGI(TAG, "Auto pairing started, 60 seconds countdown begin");
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

    g_client_ip.addr = 0; // 重置IP
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
    lv_obj_set_size(container, 200, 180);
    lv_obj_center(container);
    lv_obj_set_style_bg_color(container, lv_color_white(), 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_shadow_width(container, 20, 0);
    lv_obj_set_style_shadow_color(container, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(container, LV_OPA_50, 0);
    lv_obj_set_style_radius(container, 10, 0);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);  // 禁止滚动条显示
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE); // 禁止滚动

    // 创建旋转加载动画
    g_loading_spinner = lv_spinner_create(container, 1500,27);
    lv_obj_set_size(g_loading_spinner, 70, 70);
    lv_obj_align(g_loading_spinner, LV_ALIGN_TOP_MID, 0, 10);

    // 创建状态标签
    g_status_label = lv_label_create(container);
    lv_obj_set_width(g_status_label, 180); // 设置宽度以启用自动换行
    if (ui_get_current_language() == LANG_CHINESE) {
        lv_label_set_text(g_status_label, "正在初始化...");
    } else {
        lv_label_set_text(g_status_label, "Initializing...");
    }
    lv_obj_align(g_status_label, LV_ALIGN_BOTTOM_MID, 0, -37);
    if (is_font_loaded()) {
        lv_obj_set_style_text_font(g_status_label, get_loaded_font(), 0);
    } else {
        lv_obj_set_style_text_font(g_status_label, &lv_font_montserrat_16, 0);
    }
    lv_obj_set_style_text_color(g_status_label, lv_color_black(), 0);
    lv_obj_set_style_text_align(g_status_label, LV_TEXT_ALIGN_CENTER, 0); // 文本居中对齐

    // 创建倒计时标签
    g_countdown_label = lv_label_create(container);
    lv_label_set_text_fmt(g_countdown_label, "60");
    lv_obj_align(g_countdown_label, LV_ALIGN_BOTTOM_MID, 0, -5);
    if (is_font_loaded()) {
        lv_obj_set_style_text_font(g_countdown_label, get_loaded_font(), 0);
    } else {
        lv_obj_set_style_text_font(g_countdown_label, &lv_font_montserrat_14, 0);
    }
    lv_obj_set_style_text_color(g_countdown_label, lv_color_hex(0x666666), 0);

}

/**
 * @brief 销毁配对窗口
 */
static void destroy_pairing_window(void) {
    if (g_pairing_window) {
        lv_obj_del(g_pairing_window);
        g_pairing_window = NULL;
        g_status_label = NULL;
        g_loading_spinner = NULL;
        g_countdown_label = NULL;
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
 * @brief 倒计时定时器回调
 */
static void countdown_timer_cb(void* arg) {
    if (g_remaining_time > 0) {
        g_remaining_time--;

        // 更新倒计时标签
        if (g_countdown_label) {
            uint32_t seconds = g_remaining_time % 60;
            lv_label_set_text_fmt(g_countdown_label, "%lu", seconds);
        }

        // 检查是否有设备连接
        if (is_device_connected()) {
            // 设备连接成功
            g_pairing_state = PAIRING_STATE_SUCCESS;

            // 停止倒计时定时器
            if (g_countdown_timer) {
                esp_timer_stop(g_countdown_timer);
            }

            // 显示重启提示（双语）
            extern ui_language_t ui_get_current_language(void);
            if (ui_get_current_language() == LANG_CHINESE) {
                update_pairing_status("重启接收端中 请等待绿灯呼吸闪烁");
            } else {
                update_pairing_status("Restarting receiver. Please wait for the green light to breathe");
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
        g_remaining_time = 0;

        // 更新状态标签为超时提示
        if (ui_get_current_language() == LANG_CHINESE) {
            update_pairing_status("配对失败 - 超时");
        } else {
            update_pairing_status("Pairing failed - timeout");
        }

        // 延迟销毁窗口
        lv_timer_t* timer = lv_timer_create(pairing_window_close_cb, 2000, NULL);
        lv_timer_set_repeat_count(timer, 1);
        return;
    }
}

/**
 * @brief 窗口销毁回调函数
 */
static void pairing_window_close_cb(lv_timer_t* timer) {
    if (g_pairing_window) {
        lv_obj_del(g_pairing_window);
        g_pairing_window = NULL;
    }
    lv_timer_del(timer);
}

/**
 * @brief 启动AP热点
 */
static void start_ap_hotspot(void) {
    ESP_LOGI(TAG, "Starting AP hotspot for pairing");

    // 注册事件处理程序
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &pairing_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &pairing_event_handler, NULL));

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
        update_pairing_status("正在等待设备连接");
    } else {
        update_pairing_status("Waiting for device connection");
    }
}

/**
 * @brief 停止AP热点
 */
static void stop_ap_hotspot(void) {
    ESP_LOGI(TAG, "Stopping AP hotspot");
    // 注销事件处理程序
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, pairing_event_handler));
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, pairing_event_handler));
    ESP_ERROR_CHECK(esp_wifi_stop());
}

/**
 * @brief 事件处理程序
 */
static void pairing_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d", MAC2STR(event->mac), event->aid);
        g_client_ip.addr = 0; // 设备断开时清除IP
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_AP_STAIPASSIGNED) {
        ip_event_ap_staipassigned_t* event = (ip_event_ap_staipassigned_t*) event_data;
        ESP_LOGI(TAG, "station ip: " IPSTR, IP2STR(&event->ip));
        g_client_ip = event->ip; // 保存分配的IP地址
    }
}

/**
 * @brief 检测是否有设备连接到AP
 */
static bool is_device_connected(void) {
    return g_client_ip.addr != 0;
}

/**
 * @brief 发送TCP命令
 */
static void send_tcp_command(void) {
    if (g_client_ip.addr == 0) {
        ESP_LOGE(TAG, "Client IP not available, cannot send TCP command.");
        return;
    }

    char addr_str[16];
    inet_ntoa_r(g_client_ip, addr_str, sizeof(addr_str) - 1);

    const int port = 1100;
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = g_client_ip.addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return;
    }
    ESP_LOGI(TAG, "Socket created, connecting to %s:%d", addr_str, port);

    int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
        close(sock);
        return;
    }
    ESP_LOGI(TAG, "Successfully connected");

    // Construct reboot command based on the protocol document
    // Frame: Header(2) + Length(1) + Type(1) + Payload(N) + CRC16(2)
    // Reboot command: Type=0x05, Payload={CmdID=0x15, ParamLen=0, Params=""}
    // Data for CRC: [Length, Type, Payload]
    // Length = sizeof(Type) + sizeof(Payload) + sizeof(CRC) = 1 + 2 + 2 = 5
    // Payload = {0x15, 0x00}
    // CRC data = {0x05, 0x05, 0x15, 0x00} -> CRC16-Modbus = 0x4E85
    uint8_t reboot_cmd[] = {
        0xAA, 0x55, // Header
        0x05,       // Length
        0x05,       // Type: 特殊命令
        0x15,       // Command ID: 重启
        0x00,       // Param length
        0x4E, 0x85  // CRC16
    };

    err = send(sock, reboot_cmd, sizeof(reboot_cmd), 0);
    if (err < 0) {
        ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
    }

    ESP_LOGI(TAG, "Shutting down socket and closing connection");
    shutdown(sock, 0);
    close(sock);
    
    ESP_LOGI(TAG, "TCP command sent to receiver: REBOOT");
}

/**
 * @brief 获取配对状态
 * @return 
 */
pairing_state_t auto_pairing_get_state(void) { return g_pairing_state; }

/**
 * @brief 检查配对是否正在进行
 */
bool auto_pairing_is_active(void) { return g_pairing_state != PAIRING_STATE_IDLE; }
