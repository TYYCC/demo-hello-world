/*
 * @Author: tidycraze 2595256284@qq.com
 * @Date: 2025-09-10 13:37:58
 * @LastEditors: tidycraze 2595256284@qq.com
 * @LastEditTime: 2025-09-11 16:29:52
 * @FilePath: \demo-hello-world\main\UI\ui_image_transfer.c
 * @Description: UI图像传输模块实现文件，提供图像传输界面的创建、渲染和管理功能
 * 
 */

#include "ui_image_transfer.h"
#include "ui.h"
#include "settings_manager.h"
#include "image_transfer_app.h"
#include "display_queue.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include <string.h>
#include <stdio.h>

static const char* TAG = "UI_IMG_TRANSFER";

// UI Objects
static lv_obj_t* s_page_parent = NULL;
static lv_obj_t* s_status_label = NULL;
static lv_obj_t* s_ip_label = NULL;
static lv_obj_t* s_ssid_label = NULL;
static lv_obj_t* s_fps_label = NULL;
static lv_obj_t* s_mode_toggle_btn_label = NULL;

// Canvas for dynamic image display
static lv_obj_t* s_canvas = NULL;
static lv_color_t* s_canvas_buffer = NULL;
static uint16_t s_canvas_width = 320;
static uint16_t s_canvas_height = 240;

// Latest frame data for JPEG mode
static uint8_t* s_latest_frame_buffer = NULL;
static int s_latest_frame_width = 0;
static int s_latest_frame_height = 0;
static bool s_has_new_frame = false;
static bool s_is_rendering = false; // Flag to prevent concurrent rendering



// State variables
static bool s_is_running = false;
static image_transfer_mode_t s_current_mode;
static lv_timer_t* s_status_update_timer = NULL;
static lv_timer_t* s_image_render_timer = NULL;
// static EventGroupHandle_t s_ui_event_group = NULL;
#define FRAME_READY_BIT (1 << 0)

// Forward declarations
static void on_back_clicked(lv_event_t* e);
static void on_mode_toggle_clicked(lv_event_t* e);
static void on_settings_changed_event(lv_event_t* e);

static void start_transfer_service(image_transfer_mode_t mode);
static void stop_transfer_service(void);

static void update_mode_toggle_button(void); // Function to update the toggle button's text
static void status_update_timer_callback(lv_timer_t* timer);
static void image_render_timer_callback(lv_timer_t* timer);
static void update_ip_address(void);
static void update_ssid_label(void);

void ui_image_transfer_create(lv_obj_t* parent) {
    if (s_page_parent != NULL) {
        ESP_LOGW(TAG, "UI already created, destroying old one.");
        ui_image_transfer_destroy();
    }
    ESP_LOGI(TAG, "Creating Image Transfer UI");

    // Apply theme to the screen
    theme_apply_to_screen(parent);

    // 1. Create the page parent container
    ui_create_page_parent_container(parent, &s_page_parent);

    // 2. Create the top bar, get the settings button, and assign a callback
    lv_obj_t* top_bar_container;
    lv_obj_t* title_container;
    lv_obj_t* settings_btn = NULL; // This will hold the button created by ui_create_top_bar
    ui_create_top_bar(s_page_parent, "Image Transfer", true, &top_bar_container, &title_container,
                      &settings_btn);

    // Now, configure the button that was created for us in ui_common
    if (settings_btn) {
        // First, clean the button to remove the old icon label
        lv_obj_clean(settings_btn);

        // Then, create a new label for our text
        s_mode_toggle_btn_label = lv_label_create(settings_btn);
        lv_obj_set_style_text_font(s_mode_toggle_btn_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(s_mode_toggle_btn_label, lv_color_white(), 0);
        lv_obj_center(s_mode_toggle_btn_label);

        // Set its initial text (TCP/UDP)
        update_mode_toggle_button();

        // Assign our specific callback to the button itself
        lv_obj_add_event_cb(settings_btn, on_mode_toggle_clicked, LV_EVENT_CLICKED, NULL);
    }

    // Override the default back button callback for custom cleanup
    lv_obj_t* back_btn = lv_obj_get_child(top_bar_container, 0);
    if (back_btn) {
        lv_obj_remove_event_cb(back_btn, NULL); // Remove default callbacks
        lv_obj_add_event_cb(back_btn, on_back_clicked, LV_EVENT_CLICKED, NULL);
    }

    // 3. Create the page content area
    lv_obj_t* content_container;
    ui_create_page_content_area(s_page_parent, &content_container);

    // 4. Add page-specific content into the content_container
    // Set a flex layout to arrange panels vertically
    lv_obj_set_flex_flow(content_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    // Image display panel
    lv_obj_t* image_panel = lv_obj_create(content_container);
    lv_obj_set_width(image_panel, LV_PCT(100));
    lv_obj_set_flex_grow(image_panel, 1);
    lv_obj_set_style_pad_all(image_panel, 5, 0);
    theme_apply_to_container(image_panel);

    // Create canvas for dynamic image display
    s_canvas = lv_canvas_create(image_panel);
    lv_obj_align(s_canvas, LV_ALIGN_CENTER, 0, 0);

    // Allocate canvas buffer
    size_t buffer_size = s_canvas_width * s_canvas_height * sizeof(lv_color_t);
    s_canvas_buffer =
        (lv_color_t*)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (s_canvas_buffer) {
        // Initialize canvas with white background
        lv_canvas_set_buffer(s_canvas, s_canvas_buffer, s_canvas_width, s_canvas_height,
                             LV_IMG_CF_TRUE_COLOR);
        lv_canvas_fill_bg(s_canvas, lv_color_white(), LV_OPA_COVER);
    } else {
        ESP_LOGE(TAG, "Failed to allocate canvas buffer");
    }

    // Status display panel
    lv_obj_t* status_panel = lv_obj_create(content_container);
    lv_obj_set_width(status_panel, LV_PCT(100));
    lv_obj_set_height(status_panel, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(status_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(status_panel, 5, 0);
    theme_apply_to_container(status_panel);
    lv_obj_set_scrollbar_mode(status_panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(status_panel, LV_OBJ_FLAG_SCROLLABLE);

    s_ssid_label = lv_label_create(status_panel);
    theme_apply_to_label(s_ssid_label, false);

    s_ip_label = lv_label_create(status_panel);
    theme_apply_to_label(s_ip_label, false);

    s_status_label = lv_label_create(status_panel);
    theme_apply_to_label(s_status_label, false);

    s_fps_label = lv_label_create(status_panel);
    theme_apply_to_label(s_fps_label, false);

    // Add an event listener to the parent to catch settings changes
    lv_obj_add_event_cb(s_page_parent, on_settings_changed_event, UI_EVENT_SETTINGS_CHANGED, NULL);

    // Start the transfer service based on the current global setting
    s_current_mode = settings_get_transfer_mode();
    update_mode_toggle_button(); // Set initial text for the button
    start_transfer_service(s_current_mode);

    // Create a timer to update status labels like FPS, IP, etc.
    s_status_update_timer = lv_timer_create(status_update_timer_callback, 500, NULL);
    // Create a timer for rendering images (higher frequency to keep up with decoder)
    s_image_render_timer = lv_timer_create(image_render_timer_callback, 16, NULL); // ~60 FPS
}

void ui_image_transfer_destroy(void) {
    if (s_page_parent == NULL) {
        return;
    }

    ESP_LOGI(TAG, "Destroying Image Transfer UI");

    stop_transfer_service();

    if (s_status_update_timer) {
        lv_timer_del(s_status_update_timer);
        s_status_update_timer = NULL;
    }
    if (s_image_render_timer) {
        lv_timer_del(s_image_render_timer);
        s_image_render_timer = NULL;
    }

    // Remove all event callbacks to prevent memory access violations
    // This is critical to prevent "LoadProhibited" errors when events are processed
    // after objects have been deleted
    if (s_page_parent) {
        lv_obj_remove_event_cb(s_page_parent, on_settings_changed_event);
    }

    // Also remove event callbacks from the back button and settings button
    if (s_page_parent) {
        lv_obj_t* top_bar_container = lv_obj_get_child(s_page_parent, 0);
        if (top_bar_container) {
            lv_obj_t* back_btn = lv_obj_get_child(top_bar_container, 0);
            if (back_btn) {
                lv_obj_remove_event_cb(back_btn, on_back_clicked);
            }

            lv_obj_t* settings_btn = lv_obj_get_child(
                top_bar_container, 2); // Settings button is typically the 3rd child
            if (settings_btn) {
                lv_obj_remove_event_cb(settings_btn, on_mode_toggle_clicked);
            }
        }
    }

    // lv_obj_del will recursively delete children, so we only need to delete the parent
    lv_obj_del(s_page_parent);
    s_page_parent = NULL;

    // Reset all static pointers
    s_canvas = NULL;
    s_status_label = NULL;
    s_ip_label = NULL;
    s_ssid_label = NULL;
    s_fps_label = NULL;
    s_mode_toggle_btn_label = NULL;

    // Free canvas buffer
    if (s_canvas_buffer) {
        free(s_canvas_buffer);
        s_canvas_buffer = NULL;
    }

    // Reset JPEG frame data
    s_latest_frame_buffer = NULL;
    s_latest_frame_width = 0;
    s_latest_frame_height = 0;
    s_has_new_frame = false;
    s_is_rendering = false;

    s_is_running = false;
}

static void update_mode_toggle_button(void) {
    if (s_mode_toggle_btn_label) {
        image_transfer_mode_t current_mode = settings_get_transfer_mode();
        if (current_mode == IMAGE_TRANSFER_MODE_TCP) {
            lv_label_set_text(s_mode_toggle_btn_label, "TCP");
        } else {
            lv_label_set_text(s_mode_toggle_btn_label, "UDP");
        }
    }
}



static void start_transfer_service(image_transfer_mode_t mode) {
    if (s_is_running && mode == s_current_mode) {
        ESP_LOGI(TAG, "Service already running in mode %d", mode);
        return;
    }

    // First stop any existing service
    stop_transfer_service();

    ESP_LOGI(TAG, "Starting transfer service in mode: %s",
             mode == IMAGE_TRANSFER_MODE_TCP ? "TCP" : "UDP");

    s_current_mode = mode;
    s_is_running = true;

    // Initialize TCP-based image transfer
    image_transfer_app_init(mode);

    // Update UI labels
    update_ip_address();
    update_ssid_label();
    update_mode_toggle_button();
}

static void stop_transfer_service(void) {
    if (!s_is_running) {
        return;
    }

    ESP_LOGI(TAG, "Stopping transfer service...");

    image_transfer_app_deinit();

    s_is_running = false;
    ESP_LOGI(TAG, "Transfer service stopped.");

    if (s_status_label) {
        lv_label_set_text(s_status_label, "Status: Stopped");
    }
}

static void on_back_clicked(lv_event_t* e) {
    ESP_LOGI(TAG, "Back button clicked");
    ui_image_transfer_destroy();
    lv_obj_clean(lv_scr_act());
    ui_main_menu_create(lv_scr_act());
}

static void on_mode_toggle_clicked(lv_event_t* e) {
    // Get current mode and toggle it
    image_transfer_mode_t current_mode = settings_get_transfer_mode();
    image_transfer_mode_t new_mode = (current_mode == IMAGE_TRANSFER_MODE_TCP)
                                         ? IMAGE_TRANSFER_MODE_UDP
                                         : IMAGE_TRANSFER_MODE_TCP;

    // Set the new mode in the settings manager
    settings_set_transfer_mode(new_mode);

    // Send an event to notify that settings have changed
    // This will be caught by on_settings_changed_event to restart the service
    lv_event_send(s_page_parent, UI_EVENT_SETTINGS_CHANGED, NULL);
}

static void on_settings_changed_event(lv_event_t* e) {
    ESP_LOGI(TAG, "Settings changed event received.");
    image_transfer_mode_t new_mode = settings_get_transfer_mode();
    if (new_mode != s_current_mode) {
        ESP_LOGI(TAG, "Transfer mode changing to %s",
                 new_mode == IMAGE_TRANSFER_MODE_TCP ? "TCP" : "UDP");
        start_transfer_service(new_mode);
    }
    // Also update the toggle button text
    update_mode_toggle_button();
}

static void status_update_timer_callback(lv_timer_t* timer) {
    if (s_fps_label && s_is_running) {
        // FPS信息在新架构中不再直接提供，显示固定值
        lv_label_set_text_fmt(s_fps_label, "FPS: 0.0");
    }
    // Also update IP and SSID periodically in case it changes (e.g. reconnect)
    update_ip_address();
    update_ssid_label();
}

static void image_render_timer_callback(lv_timer_t* timer) {
    if (!s_is_running) {
        return;
    }

    // 使用新的模块化架构获取帧数据

    // 移除帧间隔限制，尽可能快地处理队列中的帧

    // 从 DisplayQueue 获取帧（JPEG/LZ4 均推送为 RGB565LE）
    // 批量处理队列中的帧，只显示最新的帧以减少延迟
    frame_msg_t msg;
    QueueHandle_t display_queue = image_transfer_app_get_display_queue();
    bool has_frame = false;
    
    if (!s_is_rendering && display_queue) {
        // 快速消费队列中的所有帧，只保留最新的一帧
        frame_msg_t temp_msg;
        while (display_queue_dequeue(display_queue, &temp_msg, 0)) {
            if (has_frame) {
                // 释放之前的帧
                display_queue_free_frame(&msg);
            }
            msg = temp_msg;
            has_frame = true;
        }
    }
    
    if (has_frame) {
        if (msg.frame_buffer && s_canvas && s_canvas_buffer) {
            s_is_rendering = true;
            uint16_t* src_ptr = (uint16_t*)msg.frame_buffer;
            lv_color_t* dst_ptr = s_canvas_buffer;
            int frame_width = msg.width;
            int frame_height = msg.height;
            int copy_width = (frame_width < s_canvas_width) ? frame_width : s_canvas_width;
            int copy_height = (frame_height < s_canvas_height) ? frame_height : s_canvas_height;
            
            // 直接复制RGB565数据，因为LVGL也使用RGB565格式
            for (int y = 0; y < copy_height; y++) {
                size_t line_bytes = (size_t)copy_width * sizeof(lv_color_t);
                memcpy(&dst_ptr[y * s_canvas_width], &src_ptr[y * frame_width], line_bytes);
            }
            lv_obj_invalidate(s_canvas);
            // 移除强制刷新，让LVGL自然调度刷新以提高性能
            s_is_rendering = false;
        }
        // 释放帧缓冲
        display_queue_free_frame(&msg);
    }
}

static void update_ip_address(void) {
    if (!s_ip_label || !s_is_running)
        return;

    char ip_str[20] = "Acquiring...";
    esp_netif_ip_info_t ip_info;
    esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif) {
        esp_err_t ret = esp_netif_get_ip_info(sta_netif, &ip_info);
        if (ret == ESP_OK && ip_info.ip.addr != 0) {
            esp_ip4addr_ntoa(&ip_info.ip, ip_str, sizeof(ip_str));
        }
    }
    lv_label_set_text_fmt(s_ip_label, "IP: %s", ip_str);
}

static void update_ssid_label(void) {
    if (!s_ssid_label || !s_is_running)
        return;

    wifi_config_t wifi_config;
    if (esp_wifi_get_config(WIFI_IF_STA, &wifi_config) == ESP_OK &&
        wifi_config.sta.ssid[0] != '\0') {
        lv_label_set_text_fmt(s_ssid_label, "SSID: %s", (char*)wifi_config.sta.ssid);
    } else {
        lv_label_set_text(s_ssid_label, "SSID: Not Connected");
    }
}