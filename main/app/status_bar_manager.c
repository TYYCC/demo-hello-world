/**
 * @file status_bar_manager.c
 * @brief 状态栏管理器实现 - 支持多个图标挨个显示
 * @author GitHub Copilot
 * @date 2025-09-01
 */

#include "../UI/inc/status_bar_manager.h"
#include "../fonts/my_font.h"
#include "audio_receiver.h"
#include "background_manager.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "wifi_manager.h"
#include <string.h>

static const char* TAG = "STATUS_BAR_MANAGER";

#define ICON_SPACING 25         // 图标之间的间距
#define BATTERY_RIGHT_OFFSET 45 // 电池图标距离右边缘的偏移

// 状态栏管理器状态结构
typedef struct {
    bool initialized;
    lv_obj_t* status_bar_container;
    lv_obj_t* time_label;
    lv_obj_t* battery_label;

    // 图标数组
    status_icon_t icons[STATUS_ICON_MAX];

    // 状态标志
    bool wifi_connected;
    bool ap_running;
    bool audio_receiving;
    bool tcp_client_connected;
    int wifi_signal_strength;

    // 更新回调
    status_bar_update_cb_t update_cb;

    // 更新定时器
    TimerHandle_t update_timer;
} status_bar_manager_t;

// 使用PSRAM分配管理器实例
static status_bar_manager_t* g_manager = NULL;

// 图标符号映射
static const char* icon_symbols[STATUS_ICON_MAX] = {
    [STATUS_ICON_WIFI_NONE] = MYSYMBOL_NO_WIFI,
    [STATUS_ICON_WIFI_LOW] = MYSYMBOL_WIFI_LOW,
    [STATUS_ICON_WIFI_MEDIUM] = MYSYMBOL_WIFI_MEDIUM,
    [STATUS_ICON_WIFI_HIGH] = MYSYMBOL_WIFI_HIGH,
    [STATUS_ICON_AP] = MYSYMBOL_BROADCAST,
    [STATUS_ICON_MUSIC] = MYSYMBOL_MUSIC,
    [STATUS_ICON_PLUGS_DISCONNECTED] = MYSYMBOL_PLUGS,
    [STATUS_ICON_PLUGS_CONNECTED] = MYSYMBOL_PLUGS_CONNECTED};

// 前向声明
static void status_bar_update_timer_callback(TimerHandle_t xTimer);
static void update_icon_positions(void);
static esp_err_t create_icon_label(status_icon_type_t icon_type);
static void hide_all_wifi_icons(void);
static void check_and_update_states(void);

/**
 * @brief 初始化状态栏管理器（基本初始化）
 */
esp_err_t status_bar_manager_init(void) {
    if (g_manager != NULL) {
        ESP_LOGW(TAG, "Status bar manager already initialized");
        return ESP_OK;
    }

    // 使用PSRAM分配内存
    g_manager =
        (status_bar_manager_t*)heap_caps_malloc(sizeof(status_bar_manager_t), MALLOC_CAP_SPIRAM);
    if (g_manager == NULL) {
        // 如果PSRAM分配失败，尝试内部RAM
        g_manager = (status_bar_manager_t*)malloc(sizeof(status_bar_manager_t));
        if (g_manager == NULL) {
            ESP_LOGE(TAG, "Failed to allocate memory for status bar manager");
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGW(TAG, "Status bar manager allocated from internal RAM");
    } else {
        ESP_LOGI(TAG, "Status bar manager allocated from PSRAM");
    }

    // 初始化状态
    memset(g_manager, 0, sizeof(status_bar_manager_t));
    g_manager->initialized = true;
    g_manager->status_bar_container = NULL;  // 稍后设置
    g_manager->update_cb = NULL;             // 稍后设置
    g_manager->wifi_signal_strength = -1;    // 表示未连接
    g_manager->tcp_client_connected = false; // 默认TCP客户端未连接

    // 初始化图标数组
    for (int i = 0; i < STATUS_ICON_MAX; i++) {
        g_manager->icons[i].type = (status_icon_type_t)i;
        g_manager->icons[i].visible = false;
        g_manager->icons[i].label = NULL;
        g_manager->icons[i].x_offset = 0;
    }

    g_manager->update_timer = NULL; // 稍后创建

    ESP_LOGI(TAG, "Status bar manager basic initialization completed");
    return ESP_OK;
}

/**
 * @brief 设置状态栏容器和回调函数
 */
esp_err_t status_bar_manager_set_container(lv_obj_t* status_bar_container,
                                           status_bar_update_cb_t update_cb) {
    if (g_manager == NULL) {
        ESP_LOGE(TAG, "Status bar manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (status_bar_container == NULL) {
        ESP_LOGE(TAG, "Status bar container cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }

    g_manager->status_bar_container = status_bar_container;
    g_manager->update_cb = update_cb;

    // 创建更新定时器（每秒检查一次）
    if (g_manager->update_timer == NULL) {
        g_manager->update_timer = xTimerCreate("status_bar_timer",
                                               pdMS_TO_TICKS(1000), // 1秒间隔
                                               pdTRUE,              // 自动重载
                                               NULL,                // 定时器ID
                                               status_bar_update_timer_callback);

        if (g_manager->update_timer == NULL) {
            ESP_LOGE(TAG, "Failed to create update timer");
            return ESP_ERR_NO_MEM;
        }
    }

    // 设置默认TCP连接状态图标（断开状态）
    status_bar_manager_set_tcp_client_status(false);

    ESP_LOGI(TAG, "Status bar manager container and timer set successfully");
    return ESP_OK;
}

/**
 * @brief 释放状态栏管理器资源
 */
void status_bar_manager_deinit(void) {
    if (g_manager == NULL) {
        return;
    }

    // 停止并删除定时器
    if (g_manager->update_timer != NULL) {
        xTimerStop(g_manager->update_timer, portMAX_DELAY);
        xTimerDelete(g_manager->update_timer, portMAX_DELAY);
    }

    // 清理图标标签
    for (int i = 0; i < STATUS_ICON_MAX; i++) {
        if (g_manager->icons[i].label != NULL && lv_obj_is_valid(g_manager->icons[i].label)) {
            lv_obj_del(g_manager->icons[i].label);
        }
    }

    free(g_manager);
    g_manager = NULL;
    ESP_LOGI(TAG, "Status bar manager deinitialized");
}

/**
 * @brief 设置时间和电池标签对象
 */
esp_err_t status_bar_manager_set_fixed_labels(lv_obj_t* time_label, lv_obj_t* battery_label) {
    if (g_manager == NULL) {
        ESP_LOGE(TAG, "Status bar manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    g_manager->time_label = time_label;
    g_manager->battery_label = battery_label;

    ESP_LOGI(TAG, "Fixed labels set successfully");
    return ESP_OK;
}

/**
 * @brief 显示指定类型的图标
 */
esp_err_t status_bar_manager_show_icon(status_icon_type_t icon_type, bool show) {
    if (g_manager == NULL) {
        ESP_LOGE(TAG, "Status bar manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (icon_type >= STATUS_ICON_MAX) {
        ESP_LOGE(TAG, "Invalid icon type: %d", icon_type);
        return ESP_ERR_INVALID_ARG;
    }

    status_icon_t* icon = &g_manager->icons[icon_type];

    if (show && !icon->visible) {
        // 需要显示图标
        if (icon->label == NULL) {
            esp_err_t ret = create_icon_label(icon_type);
            if (ret != ESP_OK) {
                return ret;
            }
        }
        icon->visible = true;
        lv_obj_clear_flag(icon->label, LV_OBJ_FLAG_HIDDEN);
        update_icon_positions();
    } else if (!show && icon->visible) {
        // 需要隐藏图标
        icon->visible = false;
        if (icon->label != NULL) {
            lv_obj_add_flag(icon->label, LV_OBJ_FLAG_HIDDEN);
        }
        update_icon_positions();
    }

    return ESP_OK;
}

/**
 * @brief 根据WiFi信号强度设置WiFi图标
 * @note STA未连接时总是显示"无WiFi"图标，即使AP正在运行
 */
esp_err_t status_bar_manager_set_wifi_signal(int signal_strength) {
    if (g_manager == NULL) {
        ESP_LOGE(TAG, "Status bar manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    g_manager->wifi_signal_strength = signal_strength;

    // 先隐藏所有WiFi图标
    hide_all_wifi_icons();

    if (signal_strength >= 0) {
        // 有WiFi连接，根据信号强度选择图标
        status_icon_type_t wifi_icon;
        if (signal_strength >= 70) {
            wifi_icon = STATUS_ICON_WIFI_HIGH;
        } else if (signal_strength >= 40) {
            wifi_icon = STATUS_ICON_WIFI_MEDIUM;
        } else {
            wifi_icon = STATUS_ICON_WIFI_LOW;
        }

        g_manager->wifi_connected = true;
        return status_bar_manager_show_icon(wifi_icon, true);
    } else {
        // STA未连接WiFi - 显示"无WiFi"图标
        g_manager->wifi_connected = false;
        return status_bar_manager_show_icon(STATUS_ICON_WIFI_NONE, true);
    }
}

/**
 * @brief 设置AP模式状态
 */
esp_err_t status_bar_manager_set_ap_status(bool is_running) {
    if (g_manager == NULL) {
        ESP_LOGE(TAG, "Status bar manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    g_manager->ap_running = is_running;
    return status_bar_manager_show_icon(STATUS_ICON_AP, is_running);
}

/**
 * @brief 设置音频接收状态
 */
esp_err_t status_bar_manager_set_audio_status(bool is_receiving) {
    if (g_manager == NULL) {
        ESP_LOGE(TAG, "Status bar manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    g_manager->audio_receiving = is_receiving;
    return status_bar_manager_show_icon(STATUS_ICON_MUSIC, is_receiving);
}

/**
 * @brief 设置TCP客户端连接状态
 */
esp_err_t status_bar_manager_set_tcp_client_status(bool has_client_connected) {
    if (g_manager == NULL) {
        ESP_LOGE(TAG, "Status bar manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    g_manager->tcp_client_connected = has_client_connected;

    // 先隐藏两个TCP状态图标
    status_bar_manager_show_icon(STATUS_ICON_PLUGS_DISCONNECTED, false);
    status_bar_manager_show_icon(STATUS_ICON_PLUGS_CONNECTED, false);

    // 根据连接状态显示对应图标
    if (has_client_connected) {
        return status_bar_manager_show_icon(STATUS_ICON_PLUGS_CONNECTED, true);
    } else {
        return status_bar_manager_show_icon(STATUS_ICON_PLUGS_DISCONNECTED, true);
    }
}

/**
 * @brief 启动状态栏更新任务
 */
esp_err_t status_bar_manager_start(void) {
    if (g_manager == NULL) {
        ESP_LOGE(TAG, "Status bar manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (xTimerStart(g_manager->update_timer, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to start update timer");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Status bar manager started");
    return ESP_OK;
}

/**
 * @brief 停止状态栏更新任务
 */
esp_err_t status_bar_manager_stop(void) {
    if (g_manager == NULL) {
        ESP_LOGE(TAG, "Status bar manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (xTimerStop(g_manager->update_timer, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to stop update timer");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Status bar manager stopped");
    return ESP_OK;
}

/**
 * @brief 获取当前显示的图标数量
 */
int status_bar_manager_get_visible_icon_count(void) {
    if (g_manager == NULL) {
        return 0;
    }

    int count = 0;
    for (int i = 0; i < STATUS_ICON_MAX; i++) {
        if (g_manager->icons[i].visible) {
            count++;
        }
    }
    return count;
}

/**
 * @brief 检查指定图标是否可见
 */
bool status_bar_manager_is_icon_visible(status_icon_type_t icon_type) {
    if (g_manager == NULL || icon_type >= STATUS_ICON_MAX) {
        return false;
    }
    return g_manager->icons[icon_type].visible;
}

/**
 * @brief 定时器回调函数 - 定期检查和更新状态
 */
static void status_bar_update_timer_callback(TimerHandle_t xTimer) {
    if (g_manager == NULL) {
        return;
    }

    check_and_update_states();

    if (g_manager->update_cb != NULL) {
        g_manager->update_cb();
    }
}

/**
 * @brief 更新图标位置 - 从右往左排列，不覆盖电池和时间
 */
static void update_icon_positions(void) {
    if (g_manager == NULL || g_manager->status_bar_container == NULL) {
        return;
    }

    int visible_count = 0;
    int current_offset = BATTERY_RIGHT_OFFSET;

    // 从右往左计算位置
    for (int i = STATUS_ICON_MAX - 1; i >= 0; i--) {
        status_icon_t* icon = &g_manager->icons[i];
        if (icon->visible && icon->label != NULL) {
            // 设置图标位置（从右边往左排列）
            lv_obj_align(icon->label, LV_ALIGN_RIGHT_MID, -current_offset, 0);
            icon->x_offset = current_offset;

            current_offset += ICON_SPACING;
            visible_count++;
        }
    }

    // 更新图标位置
}

/**
 * @brief 创建图标标签
 */
static esp_err_t create_icon_label(status_icon_type_t icon_type) {
    if (g_manager == NULL || icon_type >= STATUS_ICON_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    status_icon_t* icon = &g_manager->icons[icon_type];

    if (icon->label != NULL) {
        // 标签已存在
        return ESP_OK;
    }

    // 创建新标签
    icon->label = lv_label_create(g_manager->status_bar_container);
    if (icon->label == NULL) {
        ESP_LOGE(TAG, "Failed to create label for icon type %d", icon_type);
        return ESP_ERR_NO_MEM;
    }

    // 设置图标字体和文字
    lv_obj_set_style_text_font(icon->label, &Mysymbol, 0);
    lv_obj_set_style_text_color(icon->label, lv_color_hex(0x000000), 0);
    lv_label_set_text(icon->label, icon_symbols[icon_type]);

    // 初始状态为隐藏
    lv_obj_add_flag(icon->label, LV_OBJ_FLAG_HIDDEN);

    // 创建图标标签
    return ESP_OK;
}

/**
 * @brief 隐藏所有WiFi图标
 */
static void hide_all_wifi_icons(void) {
    if (g_manager == NULL) {
        return;
    }

    status_bar_manager_show_icon(STATUS_ICON_WIFI_NONE, false);
    status_bar_manager_show_icon(STATUS_ICON_WIFI_LOW, false);
    status_bar_manager_show_icon(STATUS_ICON_WIFI_MEDIUM, false);
    status_bar_manager_show_icon(STATUS_ICON_WIFI_HIGH, false);
}

/**
 * @brief 检查并更新所有状态
 */
static void check_and_update_states(void) {
    if (g_manager == NULL) {
        return;
    }

    // 检查STA (Station) WiFi连接状态
    wifi_manager_info_t wifi_info = wifi_manager_get_info();
    if (wifi_info.state == WIFI_STATE_CONNECTED) {
        // 获取信号强度
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            // RSSI to percentage conversion (rough approximation)
            int signal_percentage = 0;
            if (ap_info.rssi >= -50)
                signal_percentage = 100;
            else if (ap_info.rssi >= -60)
                signal_percentage = 70;
            else if (ap_info.rssi >= -70)
                signal_percentage = 40;
            else
                signal_percentage = 10;

            status_bar_manager_set_wifi_signal(signal_percentage);
        } else {
            status_bar_manager_set_wifi_signal(50); // 默认中等信号
        }
    } else {
        status_bar_manager_set_wifi_signal(-1); // 未连接
    }

    // 检查AP (Access Point) 状态 - 需要从wifi_manager获取AP运行状态
    wifi_mode_t wifi_mode;
    if (esp_wifi_get_mode(&wifi_mode) == ESP_OK) {
        bool ap_active = (wifi_mode == WIFI_MODE_AP || wifi_mode == WIFI_MODE_APSTA);
        status_bar_manager_set_ap_status(ap_active);
    }

    // 检查音频接收状态
    bool audio_active = audio_receiver_is_receiving();
    status_bar_manager_set_audio_status(audio_active);
}
