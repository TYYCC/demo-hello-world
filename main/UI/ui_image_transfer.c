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
#include "wifi_image_transfer.h"
#include "p2p_udp_image_transfer.h"
#include "image_transfer_app.h"
#include "raw_data_service.h"
#include "lz4_decoder_service.h"
#include "jpeg_decoder_service.h"


static const char* TAG = "UI_IMG_TRANSFER";

// UI Objects
static lv_obj_t* s_page_parent = NULL;
static lv_obj_t* s_status_label = NULL;
static lv_obj_t* s_ip_label = NULL;
static lv_obj_t* s_ssid_label = NULL;
static lv_obj_t* s_fps_label = NULL;
static lv_obj_t* s_mode_toggle_btn_label = NULL; // Label for the new mode toggle button

// Canvas for dynamic image display
static lv_obj_t* s_canvas = NULL;
static lv_color_t* s_canvas_buffer = NULL;
static uint16_t s_canvas_width = 240;
static uint16_t s_canvas_height = 180;

// Latest frame data for JPEG mode
static uint8_t* s_latest_frame_buffer = NULL;
static int s_latest_frame_width = 0;
static int s_latest_frame_height = 0;
static bool s_has_new_frame = false;
static bool s_is_rendering = false; // Flag to prevent concurrent rendering

// External function declaration for UI callback
void ui_image_transfer_update_jpeg_frame(const uint8_t* data, size_t length, uint16_t width, uint16_t height);

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

    // Create canvas for dynamic image display
    s_canvas = lv_canvas_create(image_panel);
    lv_obj_align(s_canvas, LV_ALIGN_CENTER, 0, 0);

    // Allocate canvas buffer
    size_t buffer_size = s_canvas_width * s_canvas_height * sizeof(lv_color_t);
    s_canvas_buffer = (lv_color_t*)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (s_canvas_buffer) {
        // Initialize canvas with white background
        lv_canvas_set_buffer(s_canvas, s_canvas_buffer, s_canvas_width, s_canvas_height, LV_IMG_CF_TRUE_COLOR);
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
    // Create a timer for rendering images (lower frequency to prevent flickering)
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
            
            lv_obj_t* settings_btn = lv_obj_get_child(top_bar_container, 2); // Settings button is typically the 3rd child
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
        ESP_LOGI(TAG, "Starting TCP service with auto-detection...");
        // 初始化所有解码器，让系统自动根据协议选择
        ret = image_transfer_app_init(IMAGE_TRANSFER_MODE_JPEG); // 默认使用JPEG，但会自动切换
        ESP_LOGI(TAG, "Base app init result: %d", ret);
        if (ret == ESP_OK) {
            // 启动所有需要的解码器服务
            esp_err_t raw_ret = image_transfer_app_set_mode(IMAGE_TRANSFER_MODE_RAW);
            ESP_LOGI(TAG, "RAW decoder init result: %d", raw_ret);

            esp_err_t lz4_ret = image_transfer_app_set_mode(IMAGE_TRANSFER_MODE_LZ4);
            ESP_LOGI(TAG, "LZ4 decoder init result: %d", lz4_ret);

            esp_err_t jpeg_ret = image_transfer_app_set_mode(IMAGE_TRANSFER_MODE_JPEG);
            ESP_LOGI(TAG, "JPEG decoder init result: %d", jpeg_ret);

            // 即使有些解码器初始化失败，我们仍然可以启动TCP服务器
            ret = image_transfer_app_start_tcp_server(6556);
            if (ret == ESP_OK) {
                lv_label_set_text(s_status_label, "Status: TCP Server Running (Auto-detect)");
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

    // Clear the canvas
    if (s_canvas && s_canvas_buffer) {
        // Fill canvas with white background to clear it
        lv_canvas_fill_bg(s_canvas, lv_color_white(), LV_OPA_COVER);
        lv_obj_invalidate(s_canvas);
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
    
    // 降低帧渲染频率，避免过度刷新
    static uint32_t last_render_time = 0;
    uint32_t current_time = lv_tick_get();
    
    // 最小帧间隔30ms (约33fps)，避免过度刷新导致的闪烁
    if (current_time - last_render_time < 30 && last_render_time != 0) {
        return;
    }
    
    // 获取最新帧数据 - 自动检测活跃的解码器
    if (raw_data_service_is_running() && !s_is_rendering && raw_data_service_get_latest_frame(&frame_buffer, &frame_size) && s_canvas && s_canvas_buffer) {
        if (frame_buffer && frame_size > 0) {
            s_is_rendering = true; // Set rendering flag

            // 假设原始数据已经是RGB565格式，直接复制到canvas缓冲区
            lv_color_t* canvas_ptr = s_canvas_buffer;
            uint16_t* rgb565_ptr = (uint16_t*)frame_buffer;

            // 计算要复制的数据大小（假设320x240分辨率）
            int copy_width = s_canvas_width;
            int copy_height = s_canvas_height;
            size_t expected_size = copy_width * copy_height * 2; // RGB565格式

            if (frame_size >= expected_size) {
                // 复制RGB565数据到canvas缓冲区
                for (int y = 0; y < copy_height; y++) {
                    for (int x = 0; x < copy_width; x++) {
                        uint16_t rgb565 = *rgb565_ptr++;
                        // Convert RGB565 to LVGL color format
                        canvas_ptr[y * s_canvas_width + x] = lv_color_make(
                            ((rgb565 >> 11) & 0x1F) << 3,  // R: 5 bits -> 8 bits
                            ((rgb565 >> 5) & 0x3F) << 2,   // G: 6 bits -> 8 bits
                            (rgb565 & 0x1F) << 3           // B: 5 bits -> 8 bits
                        );
                    }
                }

                // Force canvas refresh
                lv_obj_invalidate(s_canvas);

                ESP_LOGD(TAG, "RAW frame displayed on canvas: %zu bytes", frame_size);
            } else {
                ESP_LOGW(TAG, "RAW frame size mismatch: got %zu, expected %zu", frame_size, expected_size);
            }

            s_is_rendering = false; // Clear rendering flag
        }
        // 解锁帧数据
        raw_data_service_frame_unlock();
    } else if (lz4_decoder_service_is_running()) {
        // LZ4模式 - 使用存储的帧数据（通过回调传递）
        if (!s_is_rendering && s_has_new_frame && s_latest_frame_buffer &&
            s_latest_frame_width > 0 && s_latest_frame_height > 0 &&
            s_canvas && s_canvas_buffer) {

            // 实现帧率控制，避免过快刷新
            uint32_t current_time = xTaskGetTickCount();
            static uint32_t last_render_time = 0;
            const uint32_t min_frame_interval = pdMS_TO_TICKS(30); // 约30ms，限制为大约30fps
            
            if ((current_time - last_render_time) < min_frame_interval) {
                // 距离上次渲染时间太短，跳过此帧
                return;
            }

            s_is_rendering = true; // Set rendering flag

            // 假设LZ4解压缩后的数据已经是RGB565格式，直接复制到canvas缓冲区
            lv_color_t* canvas_ptr = s_canvas_buffer;
            uint16_t* rgb565_ptr = (uint16_t*)s_latest_frame_buffer;

            // 计算要复制的数据大小
            int copy_width = s_canvas_width;
            int copy_height = s_canvas_height;
            size_t expected_size = copy_width * copy_height * 2; // RGB565格式

            if (s_latest_frame_width * s_latest_frame_height * 2 >= expected_size) {
                // 优化的行拷贝方式，减少逐像素转换的开销
                for (int y = 0; y < copy_height && y < s_latest_frame_height; y++) {
                    uint16_t* src_line = rgb565_ptr + (y * s_latest_frame_width);
                    lv_color_t* dst_line = canvas_ptr + (y * s_canvas_width);
                    
                    // 一次处理整行数据，而不是逐像素
                    for (int x = 0; x < copy_width && x < s_latest_frame_width; x++) {
                        uint16_t rgb565 = src_line[x];
                        // 转换RGB565到LVGL颜色格式
                        dst_line[x] = lv_color_make(
                            ((rgb565 >> 11) & 0x1F) << 3,  // R: 5 bits -> 8 bits
                            ((rgb565 >> 5) & 0x3F) << 2,   // G: 6 bits -> 8 bits
                            (rgb565 & 0x1F) << 3           // B: 5 bits -> 8 bits
                        );
                    }
                }

                // 强制canvas刷新 - 使用完整刷新而不是增量刷新
                lv_obj_invalidate(s_canvas);
                
                // 等待刷新完成，避免下一帧开始时还在渲染
                lv_refr_now(NULL);
                
                ESP_LOGD(TAG, "LZ4 frame displayed: %dx%d -> %dx%d",
                        s_latest_frame_width, s_latest_frame_height,
                        s_canvas_width, s_canvas_height);
                
                // 更新渲染时间戳
                last_render_time = current_time;
            } else {
                ESP_LOGW(TAG, "LZ4 frame size too small: %dx%d", s_latest_frame_width, s_latest_frame_height);
            }

            // 重置标志
            s_has_new_frame = false;

            s_is_rendering = false; // Clear rendering flag
        }
    } else if (jpeg_decoder_service_is_running()) {
        // JPEG模式 - 使用安全的方式获取和绘制帧数据
        if (!s_is_rendering) {
            uint8_t* frame_buffer = NULL;
            int frame_width = 0;
            int frame_height = 0;
            
            // 使用新的安全方法获取帧数据（会自动处理缓冲区交换）
            if (jpeg_decoder_service_get_frame_data(&frame_buffer, &frame_width, &frame_height)) {
                if (frame_buffer && frame_width > 0 && frame_height > 0 && 
                    s_canvas && s_canvas_buffer) {
                    
                    s_is_rendering = true; // 设置渲染标志
                    
                    // 使用DMA优化的全帧复制方式
                    uint16_t* src_ptr = (uint16_t*)frame_buffer;
                    lv_color_t* dst_ptr = s_canvas_buffer;
                    int copy_width = (frame_width < s_canvas_width) ? frame_width : s_canvas_width;
                    int copy_height = (frame_height < s_canvas_height) ? frame_height : s_canvas_height;
                    
                    // 一行一行地复制，并进行颜色格式转换
                    for (int y = 0; y < copy_height; y++) {
                        for (int x = 0; x < copy_width; x++) {
                            uint16_t rgb565 = src_ptr[y * frame_width + x];
                            // 将RGB565转换为LVGL颜色格式
                            dst_ptr[y * s_canvas_width + x] = lv_color_make(
                                ((rgb565 >> 11) & 0x1F) << 3,  // R: 5位 -> 8位
                                ((rgb565 >> 5) & 0x3F) << 2,   // G: 6位 -> 8位
                                (rgb565 & 0x1F) << 3           // B: 5位 -> 8位
                            );
                        }
                    }
                    
                    // 强制canvas刷新 - 使用完整刷新而不是增量刷新
                    lv_obj_invalidate(s_canvas);
                    
                    // 等待刷新完成，避免下一帧开始时还在渲染
                    lv_refr_now(NULL);
                    
                    ESP_LOGD(TAG, "Canvas updated with triple buffer: %dx%d -> %dx%d",
                            frame_width, frame_height,
                            s_canvas_width, s_canvas_height);
                    
                    // 更新渲染时间戳
                    last_render_time = current_time;
                    
                    s_is_rendering = false; // 清除渲染标志
                }
            }
                }

                // Force canvas refresh
                lv_obj_invalidate(s_canvas);

                ESP_LOGD(TAG, "Canvas updated: %dx%d -> %dx%d",
                        s_latest_frame_width, s_latest_frame_height,
                        s_canvas_width, s_canvas_height);

                // 重置标志
                s_has_new_frame = false;

                s_is_rendering = false; // Clear rendering flag
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

// External function to update JPEG frame data from app layer
void ui_image_transfer_update_jpeg_frame(const uint8_t* data, size_t length, uint16_t width, uint16_t height) {
    if (!s_is_running) {
        return;
    }

    if (s_current_mode == IMAGE_TRANSFER_MODE_TCP && lz4_decoder_service_is_running()) {
        static size_t s_current_buffer_size = 0;
        
        // 只有当缓冲区不存在或大小不足时才重新分配
        if (s_latest_frame_buffer == NULL || s_current_buffer_size < length) {
            if (s_latest_frame_buffer) {
                free(s_latest_frame_buffer);
                s_latest_frame_buffer = NULL;
            }
            
            size_t new_size = length * 1.2;
            s_latest_frame_buffer = heap_caps_malloc(new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!s_latest_frame_buffer) {
                ESP_LOGE(TAG, "Failed to allocate frame buffer for LZ4");
                return;
            }
            s_current_buffer_size = new_size;
            ESP_LOGI(TAG, "Allocated new LZ4 buffer: %zu bytes", new_size);
        }
        
        // 复制数据到帧缓冲区
        memcpy(s_latest_frame_buffer, data, length);
        s_latest_frame_width = width;
        s_latest_frame_height = height;
        s_has_new_frame = true;
    }

    // 触发渲染过程
    if (s_image_render_timer) {
        lv_timer_reset(s_image_render_timer);
    }
}