/**
 * @file ui_serial_display.c
 * @brief 串口显示界面 - 支持接收数据、自动换行、时间戳显示
 * @author Your Name
 * @date 2024
 */
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "my_font.h"
#include "serial_display.h"
#include "ui.h"


typedef struct {
    const char* title;
    const char* wait_text;
} ui_serial_display_text_t;

static const ui_serial_display_text_t english_text = {
    .title = "Serial Display",
    .wait_text = "Waiting for connection..."
};

static const ui_serial_display_text_t chinese_text = {
    .title = "远程串口",
    .wait_text = "等待连接..."
};


static const ui_serial_display_text_t* get_current_serial_display_text(void) {
    return (g_current_language == LANG_CHINESE) ? &chinese_text : &english_text;
}


static const char* TAG = "UI_SERIAL_DISPLAY";

// 最大保存行数
#define MAX_DISPLAY_LINES 128
#define MAX_LINE_LENGTH 256  // 增加长度以避免截断
#define DISPLAY_QUEUE_LEN 16

// 消息类型
typedef struct {
    char line[MAX_LINE_LENGTH - 20];  // 为时间戳预留空间
    time_t timestamp;
} display_msg_t;

// 全局变量
static lv_obj_t* g_serial_display_screen = NULL;
static lv_obj_t* g_label = NULL;
static lv_obj_t* g_status_label = NULL;
static lv_obj_t* g_back_btn = NULL;
static lv_obj_t* g_clear_btn = NULL;

// 循环缓冲区 - 使用PSRAM动态分配
static char (*display_buffer)[MAX_LINE_LENGTH] = NULL;
static int display_start = 0;
static int display_count = 0;
static volatile bool g_ui_needs_update = false;
static lv_timer_t* g_ui_update_timer = NULL;
static bool g_buffer_initialized = false;

// 消息队列
static QueueHandle_t g_display_queue = NULL;
static TaskHandle_t g_display_task_handle = NULL;
static bool g_display_running = false;

// 初始化PSRAM缓冲区
static esp_err_t init_display_buffer(void) {
    if (g_buffer_initialized) {
        return ESP_OK;
    }

    // 分配PSRAM内存
    display_buffer = (char (*)[MAX_LINE_LENGTH])heap_caps_malloc(
        MAX_DISPLAY_LINES * MAX_LINE_LENGTH, MALLOC_CAP_SPIRAM);
    
    if (display_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate PSRAM buffer for display lines: %d bytes", 
                 MAX_DISPLAY_LINES * MAX_LINE_LENGTH);
        return ESP_ERR_NO_MEM;
    }

    // 初始化缓冲区
    memset(display_buffer, 0, MAX_DISPLAY_LINES * MAX_LINE_LENGTH);
    display_start = 0;
    display_count = 0;
    g_buffer_initialized = true;

    ESP_LOGI(TAG, "PSRAM display buffer initialized: %d lines, %d bytes", 
             MAX_DISPLAY_LINES, MAX_DISPLAY_LINES * MAX_LINE_LENGTH);
    return ESP_OK;
}

// 清理PSRAM缓冲区
static void cleanup_display_buffer(void) {
    if (display_buffer != NULL) {
        heap_caps_free(display_buffer);
        display_buffer = NULL;
    }
    g_buffer_initialized = false;
    display_start = 0;
    display_count = 0;
}

// 添加新行到循环缓冲区
static void add_line(const char* line) {
    if (!g_buffer_initialized || display_buffer == NULL) {
        ESP_LOGE(TAG, "Display buffer not initialized");
        return;
    }

    int idx = (display_start + display_count) % MAX_DISPLAY_LINES;
    strncpy(display_buffer[idx], line, MAX_LINE_LENGTH - 1);
    display_buffer[idx][MAX_LINE_LENGTH - 1] = '\0';

    if (display_count < MAX_DISPLAY_LINES) {
        display_count++;
    } else {
        display_start = (display_start + 1) % MAX_DISPLAY_LINES;
    }

    g_ui_needs_update = true;
}

// 清空显示
static void clear_display(void) {
    if (!g_buffer_initialized || display_buffer == NULL) {
        return;
    }
    
    display_start = 0;
    display_count = 0;
    memset(display_buffer, 0, MAX_DISPLAY_LINES * MAX_LINE_LENGTH);
    g_ui_needs_update = true;
}

// UI更新定时器回调
static void ui_update_timer_cb(lv_timer_t* timer) {
    if (!g_ui_needs_update || g_label == NULL || !g_buffer_initialized || display_buffer == NULL) {
        return;
    }
    g_ui_needs_update = false;

    // 检查LVGL对象是否有效
    if (!lv_obj_is_valid(g_label)) {
        ESP_LOGW(TAG, "Label object is not valid");
        return;
    }

    // 使用PSRAM分配临时缓冲区
    static char* buf = NULL;
    if (buf == NULL) {
        buf = (char*)heap_caps_malloc(MAX_DISPLAY_LINES * MAX_LINE_LENGTH, MALLOC_CAP_SPIRAM);
        if (buf == NULL) {
            ESP_LOGE(TAG, "Failed to allocate PSRAM buffer for UI update");
            return;
        }
    }

    char* p = buf;
    int rem = MAX_DISPLAY_LINES * MAX_LINE_LENGTH;

    for (int i = 0; i < display_count; i++) {
        int idx = (display_start + i) % MAX_DISPLAY_LINES;
        int n = snprintf(p, rem, "%s\n", display_buffer[idx]);
        if (n < 0 || n >= rem) break;
        p += n;
        rem -= n;
    }

    lv_label_set_text_static(g_label, buf);
    lv_obj_scroll_to_y(g_label, LV_COORD_MAX, LV_ANIM_OFF);

    // 更新状态信息
    if (g_status_label && lv_obj_is_valid(g_status_label)) {
        char status_text[128];
        bool tcp_running = serial_display_is_running();
        snprintf(status_text, sizeof(status_text), "TCP:8080 %s | Lines: %d/%d", 
                tcp_running ? "ON" : "OFF", display_count, MAX_DISPLAY_LINES);
        lv_label_set_text(g_status_label, status_text);
    }
}

// 显示任务 - 处理消息队列
static void display_task(void* pvParameters) {
    display_msg_t msg;
    TickType_t last_status_update = 0;
    const TickType_t status_update_interval = pdMS_TO_TICKS(2000); // 每2秒更新一次状态

    ESP_LOGI(TAG, "Display task started");

    while (g_display_running && g_display_queue) {
        // 等待消息，超时时间为100ms
        if (xQueueReceive(g_display_queue, &msg, pdMS_TO_TICKS(100)) == pdTRUE) {
            // 处理队列中所有剩余消息
            do {
                char line_with_ts[MAX_LINE_LENGTH];
                struct tm* tinfo = localtime(&msg.timestamp);
                if (tinfo) {
                    snprintf(line_with_ts, sizeof(line_with_ts), "[%02d:%02d:%02d] %.*s",
                             tinfo->tm_hour, tinfo->tm_min, tinfo->tm_sec, 
                             (int)(sizeof(line_with_ts) - 15), msg.line);  // 限制字符串长度
                } else {
                    snprintf(line_with_ts, sizeof(line_with_ts), "[--:--:--] %.*s", 
                             (int)(sizeof(line_with_ts) - 12), msg.line);  // 限制字符串长度
                }
                add_line(line_with_ts);
            } while (xQueueReceive(g_display_queue, &msg, 0) == pdTRUE);
        }

        // 定期检查并更新状态
        TickType_t current_time = xTaskGetTickCount();
        if (current_time - last_status_update >= status_update_interval) {
            g_ui_needs_update = true;
            last_status_update = current_time;
        }
    }

    ESP_LOGI(TAG, "Display task stopped");
    vTaskDelete(NULL);
}

// 按钮事件回调
static void back_btn_event_cb(lv_event_t* e) {
    lv_obj_t* screen = lv_scr_act();
    if (screen) {
        // 先停止显示任务
        g_display_running = false;
        if (g_display_task_handle) {
            // 等待任务结束
            vTaskDelay(pdMS_TO_TICKS(300));
            // 检查任务是否还在运行
            if (eTaskGetState(g_display_task_handle) != eDeleted) {
                vTaskDelete(g_display_task_handle);
            }
            g_display_task_handle = NULL;
        }

        // 删除消息队列
        if (g_display_queue) {
            vQueueDelete(g_display_queue);
            g_display_queue = NULL;
        }

        // 删除UI更新定时器
        if (g_ui_update_timer) {
            lv_timer_del(g_ui_update_timer);
            g_ui_update_timer = NULL;
        }

        // 清理PSRAM缓冲区
        cleanup_display_buffer();

        g_serial_display_screen = NULL;
        g_label = NULL;
        g_status_label = NULL;
        g_back_btn = NULL;
        g_clear_btn = NULL;

        // 停止TCP服务器 - 移到最后执行
        serial_display_stop();
        ESP_LOGI(TAG, "Serial display TCP server stopped on back button");

        lv_obj_clean(screen);
        ui_main_menu_create(screen);
    }
}

static void clear_btn_event_cb(lv_event_t* e) {
    clear_display();
}

// 滚动事件回调
static void scroll_event_cb(lv_event_t* e) {
    // 简化滚动事件处理，保持自动滚动行为
}

// 公共API：添加新数据
void ui_serial_display_add_data(const char* data, size_t len) {
    if (!g_display_running || !data || len == 0 || !g_display_queue) {
        return;
    }

    // 处理数据，按行分割
    char* data_copy = malloc(len + 1);
    if (!data_copy) {
        ESP_LOGE(TAG, "Failed to allocate memory for data copy");
        return;
    }

    memcpy(data_copy, data, len);
    data_copy[len] = '\0';

    char* line = strtok(data_copy, "\n\r");
    time_t current_time = time(NULL);

    while (line) {
        // 跳过空行
        if (strlen(line) > 0) {
            display_msg_t msg = {.timestamp = current_time};
            strncpy(msg.line, line, sizeof(msg.line) - 1);
            msg.line[sizeof(msg.line) - 1] = '\0';

            xQueueSend(g_display_queue, &msg, pdMS_TO_TICKS(50));
        }
        line = strtok(NULL, "\n\r");
    }

    free(data_copy);
}

// 公共API：添加文本
void ui_serial_display_add_text(const char* text) {
    if (text) {
        ui_serial_display_add_data(text, strlen(text));
    }
}

// 创建串口显示界面
void ui_serial_display_create(lv_obj_t* parent) {
    // 如果已经存在，先清理
    if (g_display_running || g_display_queue || g_display_task_handle) {
        ESP_LOGW(TAG, "Serial display already exists, cleaning up first");
        ui_serial_display_destroy();
    }
    const ui_serial_display_text_t* text = get_current_serial_display_text();

    // 初始化串口显示模块
    esp_err_t ret = serial_display_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize serial display module");
        return;
    }

    // 启动TCP服务器，监听端口8080
    if (!serial_display_start(8080)) {
        ESP_LOGE(TAG, "Failed to start serial display TCP server");
        return;
    }

    ESP_LOGI(TAG, "Serial display TCP server started on port 8080");

    // 初始化PSRAM缓冲区
    ret = init_display_buffer();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize PSRAM display buffer");
        serial_display_stop();
        return;
    }

    // 应用当前主题到屏幕
    theme_apply_to_screen(parent);

    // 1. 创建页面父级容器（统一管理整个页面）
    lv_obj_t* page_parent_container;
    ui_create_page_parent_container(parent, &page_parent_container);

    // 设置全局屏幕变量
    g_serial_display_screen = parent;

    // 2. 创建顶部栏容器（包含返回按钮和标题）
    lv_obj_t* top_bar_container;
    lv_obj_t* title_container;
    ui_create_top_bar(page_parent_container, text->title, false, &top_bar_container, &title_container, NULL);

    // 替换顶部栏的返回按钮回调为自定义回调
    lv_obj_t* back_btn = lv_obj_get_child(top_bar_container, 0); // 获取返回按钮
    if (back_btn) {
        lv_obj_remove_event_cb(back_btn, NULL); // 移除默认回调
        lv_obj_add_event_cb(back_btn, back_btn_event_cb, LV_EVENT_CLICKED, NULL);
        g_back_btn = back_btn; // Save reference
    }

    // 3. 创建页面内容容器
    lv_obj_t* content_container;
    ui_create_page_content_area(page_parent_container, &content_container);

    // 创建标签 - 直接占满整个内容区域
    g_label = lv_label_create(content_container);
    lv_obj_set_size(g_label, 240, 290);
    lv_obj_align(g_label, LV_ALIGN_CENTER, 0, 10);
    lv_label_set_long_mode(g_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(g_label, text->wait_text);
    lv_obj_set_style_text_color(g_label, theme_get_color(theme_get_current_theme()->colors.text_primary), 0);
    lv_obj_set_style_bg_color(g_label, theme_get_color(theme_get_current_theme()->colors.surface), 0);
    set_language_display(g_label);

    // 添加滚动事件
    lv_obj_add_event_cb(g_label, scroll_event_cb, LV_EVENT_SCROLL, NULL);


    // 5. 创建清空按钮 - 直接放在内容区域右下角
    g_clear_btn = lv_btn_create(content_container);
    lv_obj_set_size(g_clear_btn, 50, 20);                     // 很小的按钮
    lv_obj_align(g_clear_btn, LV_ALIGN_BOTTOM_RIGHT, -5, -5); // 右下角
    theme_apply_to_button(g_clear_btn, true);
    lv_obj_add_event_cb(g_clear_btn, clear_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* clear_label = lv_label_create(g_clear_btn);
    lv_label_set_text(clear_label, "C");
    lv_obj_center(clear_label);

    // 初始化数据
    clear_display();

    // 创建消息队列
    g_display_queue = xQueueCreate(DISPLAY_QUEUE_LEN, sizeof(display_msg_t));
    if (!g_display_queue) {
        ESP_LOGE(TAG, "Failed to create display queue");
        return;
    }

    // 创建UI更新定时器
    g_ui_update_timer = lv_timer_create(ui_update_timer_cb, 100, NULL); // 100ms刷新率
    if (g_ui_update_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create LVGL UI update timer");
        ui_serial_display_destroy(); // Clean up
        return;
    }

    // 启动显示任务
    g_display_running = true;
    if (xTaskCreate(display_task, "ui_display_task", 4096, NULL, 3, &g_display_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create display task");
        g_display_running = false;
        vQueueDelete(g_display_queue);
        g_display_queue = NULL;
        serial_display_stop();
        return;
    }

    ESP_LOGI(TAG, "Serial display UI created successfully");
}

// 销毁串口显示界面
void ui_serial_display_destroy(void) {
    // 停止TCP服务器
    serial_display_stop();
    ESP_LOGI(TAG, "Serial display TCP server stopped");

    // 停止显示任务
    g_display_running = false;
    if (g_display_task_handle) {
        // 等待任务结束
        vTaskDelay(pdMS_TO_TICKS(200));
        // 检查任务是否还在运行
        if (eTaskGetState(g_display_task_handle) != eDeleted) {
            vTaskDelete(g_display_task_handle);
        }
        g_display_task_handle = NULL;
    }

    // 删除消息队列
    if (g_display_queue) {
        vQueueDelete(g_display_queue);
        g_display_queue = NULL;
    }

    // 删除UI更新定时器
    if (g_ui_update_timer) {
        lv_timer_del(g_ui_update_timer);
        g_ui_update_timer = NULL;
    }

    // 清理PSRAM缓冲区
    cleanup_display_buffer();

    // 清空全局变量
    g_serial_display_screen = NULL;
    g_label = NULL;
    g_status_label = NULL;
    g_back_btn = NULL;
    g_clear_btn = NULL;

    ESP_LOGI(TAG, "Serial display UI destroyed");
}
