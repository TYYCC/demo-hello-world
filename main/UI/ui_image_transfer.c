/**
 * @file ui_image_transfer.c
 * @brief Unified image transfer UI (UDP/TCP) that follows the UI template guide.
 * @author TidyCraze
 * @date 2025-08-15
 */
#include "ui_image_transfer.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include <stdio.h>
#include <string.h>

#include "ui_image_transfer.h"
#include "theme_manager.h"
#include "ui.h"
#include "ui_common.h"
#include "settings_manager.h"
#include "wifi_image_transfer.h" // For TCP mode
#include "p2p_udp_image_transfer.h" // For UDP mode
#include "image_transfer_app.h" // 新的模块化图像传输应用
#include "../../app/image_transfer/inc/raw_data_service.h"


static const char* TAG = "UI_IMG_TRANSFER";

// UI Objects
static lv_obj_t* s_page_parent = NULL;
static lv_obj_t* s_img_obj = NULL;
static lv_obj_t* s_status_label = NULL;
static lv_obj_t* s_ip_label = NULL;
static lv_obj_t* s_ssid_label = NULL;
static lv_obj_t* s_fps_label = NULL;
static lv_obj_t* s_mode_toggle_btn_label = NULL; // Label for the new mode toggle button

// Image descriptor
static lv_img_dsc_t s_img_dsc = {.header.always_zero = 0,
                                 .header.w = 0,
                                 .header.h = 0,
                                 .header.cf = LV_IMG_CF_TRUE_COLOR,
                                 .data_size = 0,
                                 .data = NULL};

// State variables
static bool s_is_running = false;
static image_transfer_mode_t s_current_mode;
static lv_timer_t* s_status_update_timer = NULL;
static lv_timer_t* s_image_render_timer = NULL;
static EventGroupHandle_t s_ui_event_group = NULL;
#define FRAME_READY_BIT (1 << 0)

// Forward declarations
static void on_back_clicked(lv_event_t* e);
static void on_mode_toggle_clicked(lv_event_t* e); 
static void on_settings_changed_event(lv_event_t* e);

static void start_transfer_service(image_transfer_mode_t mode);
static void stop_transfer_service(void);

static void update_mode_toggle_button(void); // Function to update the toggle button's text

static void udp_status_callback(p2p_connection_state_t state, const char* info);
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
    ui_create_top_bar(s_page_parent, "Image Transfer", true, &top_bar_container, &title_container, &settings_btn);

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
    lv_obj_set_flex_align(content_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Image display panel
    lv_obj_t* image_panel = lv_obj_create(content_container);
    lv_obj_set_width(image_panel, LV_PCT(100));
    lv_obj_set_flex_grow(image_panel, 1);
    lv_obj_set_style_pad_all(image_panel, 5, 0);
    theme_apply_to_container(image_panel);

    s_img_obj = lv_img_create(image_panel);
    lv_obj_align(s_img_obj, LV_ALIGN_CENTER, 0, 0);

    // Status display panel
    lv_obj_t* status_panel = lv_obj_create(content_container);
    lv_obj_set_width(status_panel, LV_PCT(100));
    lv_obj_set_height(status_panel, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(status_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(status_panel, 5, 0);
    theme_apply_to_container(status_panel);
    
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
    // Create a high-frequency timer for rendering images
    s_image_render_timer = lv_timer_create(image_render_timer_callback, 33, NULL); // ~30 FPS
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

    // lv_obj_del will recursively delete children, so we only need to delete the parent
    lv_obj_del(s_page_parent);
    s_page_parent = NULL;

    // Reset all static pointers
    s_img_obj = NULL;
    s_status_label = NULL;
    s_ip_label = NULL;
    s_ssid_label = NULL;
    s_fps_label = NULL;
    s_mode_toggle_btn_label = NULL;

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

static void udp_status_callback(p2p_connection_state_t state, const char* info)
{
    const char* state_str = "Unknown";
    switch (state) {
        case P2P_STATE_IDLE: state_str = "Idle"; break;
        case P2P_STATE_AP_STARTING: state_str = "Starting AP..."; break;
        case P2P_STATE_AP_RUNNING: state_str = "AP Running"; break;
        case P2P_STATE_STA_CONNECTING: state_str = "Connecting..."; break;
        case P2P_STATE_STA_CONNECTED: state_str = "Connected"; break;
        case P2P_STATE_ERROR: state_str = "Error"; break;
    }
     if (s_status_label) {
        lv_label_set_text_fmt(s_status_label, "Status: %s", state_str);
    }
}

static void start_transfer_service(image_transfer_mode_t mode) {
    if (s_is_running && mode == s_current_mode) {
        ESP_LOGW(TAG, "Service for the selected mode is already running.");
        update_ssid_label();
        update_ip_address();
        return;
    }
    
    if (s_is_running) {
        stop_transfer_service();
        // A small delay to allow tasks and sockets to close gracefully before restarting
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    s_current_mode = mode;
    esp_err_t ret = ESP_FAIL;

    if (mode == IMAGE_TRANSFER_MODE_UDP) {
        ESP_LOGI(TAG, "Initializing UDP service...");
        ret = p2p_udp_image_transfer_init(P2P_MODE_STA, NULL, udp_status_callback);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Starting UDP service...");
            ret = p2p_udp_image_transfer_start();
        }
    } else { // IMAGE_TRANSFER_MODE_TCP
        ESP_LOGI(TAG, "Starting TCP service...");
        // 使用新的模块化图像传输应用
        ret = image_transfer_app_init(IMAGE_TRANSFER_MODE_RAW); // 默认使用原始模式
        if (ret == ESP_OK) {
            ret = image_transfer_app_start_tcp_server(6556);
            if (ret == ESP_OK) {
                lv_label_set_text(s_status_label, "Status: TCP Server Running");
            }
        }
    }

    if (ret == ESP_OK) {
        s_is_running = true;
        ESP_LOGI(TAG, "%s service started.", mode == IMAGE_TRANSFER_MODE_UDP ? "UDP" : "TCP");
    } else {
        ESP_LOGE(TAG, "Failed to start %s service.", mode == IMAGE_TRANSFER_MODE_UDP ? "UDP" : "TCP");
        lv_label_set_text(s_status_label, "Status: Start failed");
    }
    update_ssid_label();
    update_ip_address();
}

static void stop_transfer_service(void) {
    if (!s_is_running) {
        return;
    }
    ESP_LOGI(TAG, "Stopping %s service...", s_current_mode == IMAGE_TRANSFER_MODE_UDP ? "UDP" : "TCP");

    if (s_current_mode == IMAGE_TRANSFER_MODE_UDP) {
        p2p_udp_image_transfer_deinit();
    } else { // IMAGE_TRANSFER_MODE_TCP
        image_transfer_app_stop_tcp_server();
        image_transfer_app_deinit();
        s_ui_event_group = NULL;
    }

    s_is_running = false;
    lv_label_set_text(s_status_label, "Status: Stopped");
    lv_label_set_text(s_ip_label, "IP: Not Assigned");
    lv_label_set_text(s_ssid_label, "SSID: -");
    lv_label_set_text(s_fps_label, "FPS: 0.0");

    // Clear the image
    if (s_img_obj) {
        lv_img_set_src(s_img_obj, NULL);
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
    image_transfer_mode_t new_mode = (current_mode == IMAGE_TRANSFER_MODE_TCP) ? IMAGE_TRANSFER_MODE_UDP : IMAGE_TRANSFER_MODE_TCP;
    
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
        ESP_LOGI(TAG, "Transfer mode changing to %s", new_mode == IMAGE_TRANSFER_MODE_TCP ? "TCP" : "UDP");
        start_transfer_service(new_mode);
    }
    // Also update the toggle button text
    update_mode_toggle_button();
}

static void status_update_timer_callback(lv_timer_t* timer)
{
    if (s_fps_label && s_is_running) {
        float fps = 0.0f;
        if (s_current_mode == IMAGE_TRANSFER_MODE_UDP) {
            fps = p2p_udp_get_fps();
        } else {
            fps = wifi_image_transfer_get_fps();
        }
        lv_label_set_text_fmt(s_fps_label, "FPS: %d.%01d", (int)fps, (int)(fps * 10) % 10);
    }
    // Also update IP and SSID periodically in case it changes (e.g. reconnect)
    update_ip_address();
    update_ssid_label();
}

static void image_render_timer_callback(lv_timer_t* timer)
{
    if (!s_is_running || s_current_mode != IMAGE_TRANSFER_MODE_TCP) {
        return;
    }

    // 使用新的模块化架构获取帧数据
    uint8_t* frame_buffer = NULL;
    size_t frame_size = 0;
    
    // 获取最新帧数据
    if (image_transfer_app_get_mode() == IMAGE_TRANSFER_MODE_RAW) {
        // 原始模式
        if (raw_data_service_get_latest_frame(&frame_buffer, &frame_size)) {
            if (frame_buffer && frame_size > 0) {
                if (s_img_obj && lv_obj_is_valid(s_img_obj)) {
                    // 假设原始数据已经是RGB565格式
                    s_img_dsc.header.w = 320; // 默认宽度
                    s_img_dsc.header.h = 240; // 默认高度
                    s_img_dsc.data_size = frame_size;
                    s_img_dsc.data = frame_buffer;
                    
                    lv_img_set_src(s_img_obj, &s_img_dsc);
                }
            }
            // 解锁帧数据
            raw_data_service_frame_unlock();
        }
    }
}

static void update_ip_address(void) {
    if (!s_ip_label || !s_is_running) return;

    char ip_str[20] = "Acquiring...";
    esp_err_t ret = ESP_FAIL;
    
    if (s_current_mode == IMAGE_TRANSFER_MODE_UDP) {
        ret = p2p_udp_get_local_ip(ip_str, sizeof(ip_str));
    } else {
        esp_netif_ip_info_t ip_info;
        esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (sta_netif) {
            ret = esp_netif_get_ip_info(sta_netif, &ip_info);
            if (ret == ESP_OK && ip_info.ip.addr != 0) {
                esp_ip4addr_ntoa(&ip_info.ip, ip_str, sizeof(ip_str));
            }
        }
    }
    lv_label_set_text_fmt(s_ip_label, "IP: %s", ip_str);
}

static void update_ssid_label(void) {
    if (!s_ssid_label || !s_is_running) return;

    if (s_current_mode == IMAGE_TRANSFER_MODE_UDP) {
        uint8_t mac[6];
        esp_wifi_get_mac(WIFI_IF_AP, mac);
        lv_label_set_text_fmt(s_ssid_label, "SSID: %s%02X%02X", P2P_WIFI_SSID_PREFIX, mac[4], mac[5]);
    } else {
        wifi_config_t wifi_config;
        if (esp_wifi_get_config(WIFI_IF_STA, &wifi_config) == ESP_OK && wifi_config.sta.ssid[0] != '\0') {
            lv_label_set_text_fmt(s_ssid_label, "SSID: %s", (char*)wifi_config.sta.ssid);
        } else {
            lv_label_set_text(s_ssid_label, "SSID: Not Connected");
        }
    }
}