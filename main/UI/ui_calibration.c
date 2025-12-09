/**
 * @file ui_calibration.c
 * @brief Calibration and test interface - supports calibration and testing of various peripherals
 * @author Your Name
 * @date 2024
 */
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "calibration_manager.h"
#include "joystick_adc.h"
#include "lsm6ds3.h"
#include "lsm6ds_control.h"
#include "my_font.h"
#include "theme_manager.h"
#include "ui.h"

static const char* TAG = "UI_CALIBRATION";

// Canvas definitions
#define CANVAS_WIDTH 120
#define CANVAS_HEIGHT 120
static lv_color_t* canvas_buf = NULL;

// UI state
typedef enum {
    CALIBRATION_STATE_MAIN_MENU,
    CALIBRATION_STATE_JOYSTICK_TEST,
    CALIBRATION_STATE_GYROSCOPE_TEST
} calibration_state_t;

// Forward declarations
static void create_main_menu(lv_obj_t* content_container);
static void create_joystick_test(lv_obj_t* content_container);
static void create_gyroscope_test(lv_obj_t* content_container);
static void test_task(void* pvParameter);
static void ui_update_timer_cb(lv_timer_t* timer);

// Global variables
static lv_obj_t* g_page_parent_container = NULL;
static lv_obj_t* g_content_container = NULL;
static lv_obj_t* g_info_label = NULL;

// Current state
static calibration_state_t g_current_state = CALIBRATION_STATE_MAIN_MENU;
static bool g_test_running = false;
static TaskHandle_t g_test_task_handle = NULL;
static lv_timer_t* g_ui_update_timer = NULL;
static QueueHandle_t g_test_queue = NULL;

// Message types
typedef enum { 
    MSG_UPDATE_JOYSTICK, 
    MSG_UPDATE_GYROSCOPE,      // Gyroscope angular velocity data
    MSG_UPDATE_EULER,          // Euler angles from background Fusion AHRS
    MSG_STOP_TEST 
} test_msg_type_t;

// Joystick test data structure
typedef struct {
    lv_obj_t* joystick_indicator;
    lv_obj_t* value_label;
} joystick_test_data_t;

// 3D point structure
typedef struct {
    float x, y, z;
} point3d_t;

// Gyroscope test data structure
typedef struct {
    lv_obj_t* canvas;
    lv_obj_t* value_label;
    point3d_t initial_vertices[8];   // Initial cube vertices
    float angle_x, angle_y, angle_z; // Rotation angles (using improved integration algorithm)
} gyro_test_data_t;

typedef struct {
    test_msg_type_t type;
    union {
        struct {
            int16_t joy1_x, joy1_y;
        } joystick;
        struct {
            float x, y, z;  // Angular velocity (dps)
        } gyro;
        struct {
            float roll, pitch, yaw;  // Euler angles (degrees) - read from background Fusion AHRS
        } euler;
    } data;
} test_msg_t;

// Custom back button callback - handles special logic for calibration interface
static void calibration_back_btn_callback(lv_event_t* e) {
    if (g_test_running) {
        // Stop test
        test_msg_t msg = {.type = MSG_STOP_TEST};
        xQueueSend(g_test_queue, &msg, pdMS_TO_TICKS(100));
        g_test_running = false;

        // Delete UI update timer
        if (g_ui_update_timer) {
            lv_timer_del(g_ui_update_timer);
            g_ui_update_timer = NULL;
        }
    }

    if (g_current_state == CALIBRATION_STATE_MAIN_MENU) {
        // Return to main menu
        lv_obj_t* screen = lv_scr_act();
        if (screen) {
            lv_obj_clean(screen);
            ui_main_menu_create(screen);
        }
    } else {
        // Return to calibration main menu, refresh display with latest status
        g_current_state = CALIBRATION_STATE_MAIN_MENU;
        ESP_LOGI(TAG, "Returning to calibration main menu, refreshing status");
        create_main_menu(g_content_container);
    }
}

static void calibrate_btn_event_cb(lv_event_t* e) {
    esp_err_t ret = ESP_OK;

    switch (g_current_state) {
    case CALIBRATION_STATE_JOYSTICK_TEST:
        ret = calibrate_joystick();
        break;
    case CALIBRATION_STATE_GYROSCOPE_TEST:
        ret = calibrate_gyroscope();
        break;
    default:
        break;
    }

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration completed successfully");
        
        const calibration_status_t* status = get_calibration_status();
        if (status) {
            ESP_LOGI(TAG, "Updated calibration status: Gyro=%d", 
                     status->gyroscope_calibrated);
        }
        
        if (g_current_state == CALIBRATION_STATE_MAIN_MENU) {
            create_main_menu(g_content_container);
        }
    } else {
        ESP_LOGE(TAG, "Calibration failed: %s", esp_err_to_name(ret));
    }
}

static void test_btn_event_cb(lv_event_t* e) {
    if (g_test_running) {

        g_test_running = false;

        test_msg_t msg = {.type = MSG_STOP_TEST};
        xQueueSend(g_test_queue, &msg, pdMS_TO_TICKS(100));

        if (g_ui_update_timer) {
            lv_timer_del(g_ui_update_timer);
            g_ui_update_timer = NULL;
        }

        lv_obj_t* btn = lv_event_get_target(e);
        lv_obj_t* label = lv_obj_get_child(btn, 0);
        lv_label_set_text(label, "Start Test");

        ESP_LOGI(TAG, "Test stopped");
    } else {
        // Start test
        g_test_running = true;

        // Start test task
        if (g_test_task_handle == NULL) {
            BaseType_t ret = xTaskCreate(test_task, "test_task", 4096, NULL, 5, &g_test_task_handle);
            if (ret != pdPASS) {
                ESP_LOGE(TAG, "Failed to create test task");
                g_test_running = false;
                return;
            }
        }

        // Create UI update timer (20ms = 50Hz, improve response speed)
        g_ui_update_timer = lv_timer_create(ui_update_timer_cb, 20, g_test_queue);

        lv_obj_t* btn = lv_event_get_target(e);
        lv_obj_t* label = lv_obj_get_child(btn, 0);
        lv_label_set_text(label, "Stop Test");

        ESP_LOGI(TAG, "Test started");
    }
}

static void menu_btn_event_cb(lv_event_t* e) {
    lv_obj_t* btn = lv_event_get_target(e);
    int index = (int)(intptr_t)lv_obj_get_user_data(btn);

    switch (index) {
    case 0: // Joystick Test
        g_current_state = CALIBRATION_STATE_JOYSTICK_TEST;
        create_joystick_test(g_content_container);
        break;
    case 1: // Gyroscope Test
        g_current_state = CALIBRATION_STATE_GYROSCOPE_TEST;
        create_gyroscope_test(g_content_container);
        break;
    }
}

// Create main menu
static void create_main_menu(lv_obj_t* content_container) {
    if (!content_container)
        return;

    lv_obj_clean(content_container);

    // [Critical] Get the latest calibration status
    // Ensure the displayed status is loaded from NVS
    const calibration_status_t* status = get_calibration_status();
    if (status) {
        ESP_LOGI(TAG, "Displaying calibration status: Gyro=%d", 
                 status->gyroscope_calibrated);
        char status_text[256];
        snprintf(status_text, sizeof(status_text),
                 "校准状态:\n"
                 "摇杆: %s\n"
                 "陀螺仪: %s",
                 status->joystick_calibrated ? "已校准" : "未校准", 
                 status->gyroscope_calibrated ? "已校准" : "未校准");

        g_info_label = lv_label_create(content_container);
        lv_label_set_text(g_info_label, status_text);
        theme_apply_to_label(g_info_label, false);
        lv_obj_align(g_info_label, LV_ALIGN_TOP_MID, 0, 10);
        lv_font_t* loaded_font = get_loaded_font();
        lv_obj_set_style_text_font(g_info_label, loaded_font, 0);
    }

    // Create menu buttons
    const char* menu_items[] = {"Joystick Test", "Gyroscope Test"};

    for (int i = 0; i < 2; i++) {
        lv_obj_t* btn = lv_btn_create(content_container);
        lv_obj_set_size(btn, 200, 40);
        lv_obj_align(btn, LV_ALIGN_CENTER, 0, 60 + i * 50);
        theme_apply_to_button(btn, true);

        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, menu_items[i]);
        lv_obj_center(label);

        // Set button event
        lv_obj_set_user_data(btn, (void*)(intptr_t)i);
        lv_obj_add_event_cb(btn, menu_btn_event_cb, LV_EVENT_CLICKED, NULL);
    }
}

// Create calibration interface
void ui_calibration_create(lv_obj_t* parent) {
    g_current_state = CALIBRATION_STATE_MAIN_MENU;

    // Allocate canvas buffer from PSRAM
    canvas_buf = heap_caps_malloc(LV_CANVAS_BUF_SIZE_TRUE_COLOR(CANVAS_WIDTH, CANVAS_HEIGHT), MALLOC_CAP_SPIRAM);
    if (!canvas_buf) {
        ESP_LOGE(TAG, "Failed to allocate canvas buffer in PSRAM");
        return;
    }

    // Create message queue
    g_test_queue = xQueueCreate(10, sizeof(test_msg_t));
    if (!g_test_queue) {
        ESP_LOGE(TAG, "Failed to create test queue");
        heap_caps_free(canvas_buf); // If queue creation fails, free the buffer
        canvas_buf = NULL;
        return;
    }

    // Apply current theme to screen
    theme_apply_to_screen(parent);

    // 1. Create page parent container (unified management of the entire page)
    ui_create_page_parent_container(parent, &g_page_parent_container);

    // 2. Create top bar container (contains back button and title)
    lv_obj_t* top_bar_container;
    lv_obj_t* title_container;
    ui_create_top_bar(g_page_parent_container, "Calibration & Test", false, &top_bar_container, &title_container, NULL);

    // Replace top bar back button callback with custom callback
    lv_obj_t* back_btn = lv_obj_get_child(top_bar_container, 0); // Get back button
    if (back_btn) {
        lv_obj_remove_event_cb(back_btn, NULL); // Remove default callback
        lv_obj_add_event_cb(back_btn, calibration_back_btn_callback, LV_EVENT_CLICKED, NULL);
    }

    // 3. Create page content container
    ui_create_page_content_area(g_page_parent_container, &g_content_container);

    // 4. Create main menu
    create_main_menu(g_content_container);

    ESP_LOGI(TAG, "Calibration UI created successfully");
}

// Destroy calibration interface
void ui_calibration_destroy(void) {
    // Stop test
    if (g_test_running) {
        test_msg_t msg = {.type = MSG_STOP_TEST};
        xQueueSend(g_test_queue, &msg, pdMS_TO_TICKS(100));
        g_test_running = false;
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Delete test task
    if (g_test_task_handle) {
        vTaskDelete(g_test_task_handle);
        g_test_task_handle = NULL;
    }

    // Delete UI update timer
    if (g_ui_update_timer) {
        lv_timer_del(g_ui_update_timer);
        g_ui_update_timer = NULL;
    }

    // Free test data memory
    if (g_content_container && g_current_state == CALIBRATION_STATE_JOYSTICK_TEST) {
        joystick_test_data_t* test_data = (joystick_test_data_t*)lv_obj_get_user_data(g_content_container);
        if (test_data) {
            lv_mem_free(test_data);
        }
    } else if (g_content_container && g_current_state == CALIBRATION_STATE_GYROSCOPE_TEST) {
        gyro_test_data_t* test_data = (gyro_test_data_t*)lv_obj_get_user_data(g_content_container);
        if (test_data) {
            lv_mem_free(test_data);
        }
    }

    // Delete message queue
    if (g_test_queue) {
        vQueueDelete(g_test_queue);
        g_test_queue = NULL;
    }

    // Free canvas buffer
    if (canvas_buf) {
        heap_caps_free(canvas_buf);
        canvas_buf = NULL;
    }

    // Clear global variables
    g_page_parent_container = NULL;
    g_content_container = NULL;
    g_info_label = NULL;

    ESP_LOGI(TAG, "Calibration UI destroyed");
}

// Create joystick test interface
static void create_joystick_test(lv_obj_t* content_container) {
    if (!content_container)
        return;

    lv_obj_clean(content_container);

    // Create joystick display area
    lv_obj_t* joystick_area = lv_obj_create(content_container);
    lv_obj_set_size(joystick_area, 120, 120);
    lv_obj_align(joystick_area, LV_ALIGN_CENTER, 0, -40);
    lv_obj_set_style_bg_color(joystick_area, lv_color_hex(0x34495E), 0);
    lv_obj_set_style_bg_opa(joystick_area, LV_OPA_50, 0);
    lv_obj_set_style_radius(joystick_area, 60, 0);
    lv_obj_set_style_border_width(joystick_area, 2, 0);
    lv_obj_set_style_border_color(joystick_area, lv_color_hex(0x95A5A6), 0);

    // 创建摇杆指示器
    lv_obj_t* joystick_indicator = lv_obj_create(joystick_area);
    lv_obj_set_size(joystick_indicator, 15, 15);
    lv_obj_align(joystick_indicator, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(joystick_indicator, lv_color_hex(0xE74C3C), 0);
    lv_obj_set_style_radius(joystick_indicator, 8, 0);

    // Create joystick label
    lv_obj_t* joy_label = lv_label_create(content_container);
    lv_label_set_text(joy_label, "Joystick");
    lv_obj_align(joy_label, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_text_font(joy_label, &lv_font_montserrat_14, 0);

    // Create value display label
    lv_obj_t* value_label = lv_label_create(content_container);
    lv_label_set_text(value_label, "X: 0  Y: 0");
    lv_obj_align(value_label, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_CENTER, 0);

    // Save control references for update
    joystick_test_data_t* test_data = lv_mem_alloc(sizeof(joystick_test_data_t));
    test_data->joystick_indicator = joystick_indicator;
    test_data->value_label = value_label;
    lv_obj_set_user_data(content_container, test_data);

    // Create button container
    lv_obj_t* btn_cont = lv_obj_create(content_container);
    lv_obj_set_size(btn_cont, 200, 50);
    lv_obj_align(btn_cont, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_bg_opa(btn_cont, LV_OPA_0, 0);
    lv_obj_set_style_border_width(btn_cont, 0, 0);
    lv_obj_set_style_pad_all(btn_cont, 0, 0);

    // Calibrate button
    lv_obj_t* calibrate_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(calibrate_btn, 80, 40);
    lv_obj_align(calibrate_btn, LV_ALIGN_LEFT_MID, 10, 0);
    theme_apply_to_button(calibrate_btn, true);
    lv_obj_add_event_cb(calibrate_btn, calibrate_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* calibrate_label = lv_label_create(calibrate_btn);
    lv_label_set_text(calibrate_label, "Calibrate");
    lv_obj_center(calibrate_label);

    // Test button
    lv_obj_t* test_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(test_btn, 80, 40);
    lv_obj_align(test_btn, LV_ALIGN_RIGHT_MID, -10, 0);
    theme_apply_to_button(test_btn, true);
    lv_obj_add_event_cb(test_btn, test_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* test_label = lv_label_create(test_btn);
    lv_label_set_text(test_label, "Start Test");
    lv_obj_center(test_label);

    ESP_LOGI(TAG, "Joystick test interface created");
}

// Draw cube on canvas
static void draw_cube_on_canvas(lv_obj_t* canvas, point3d_t* initial_vertices, float angle_x, float angle_y,
                                float angle_z) {
    lv_canvas_fill_bg(canvas, lv_color_hex(0x34495E), LV_OPA_COVER);

    point3d_t rotated_vertices[8];
    // Rotation matrix calculation
    float sin_ax = sinf(angle_x), cos_ax = cosf(angle_x);
    float sin_ay = sinf(angle_y), cos_ay = cosf(angle_y);
    float sin_az = sinf(angle_z), cos_az = cosf(angle_z);

    for (int i = 0; i < 8; i++) {
        point3d_t p = initial_vertices[i];

        // Rotate around X axis
        float y = p.y * cos_ax - p.z * sin_ax;
        float z = p.y * sin_ax + p.z * cos_ax;
        p.y = y;
        p.z = z;

        // Rotate around Y axis
        float x = p.x * cos_ay + p.z * sin_ay;
        z = -p.x * sin_ay + p.z * cos_ay;
        p.x = x;
        p.z = z;

        // Rotate around Z axis
        x = p.x * cos_az - p.y * sin_az;
        y = p.x * sin_az + p.y * cos_az;
        p.x = x;
        p.y = y;

        rotated_vertices[i] = p;
    }

    // Project and draw
    lv_point_t projected_points[8];
    for (int i = 0; i < 8; i++) {
        // Orthogonal projection
        projected_points[i].x = (int16_t)(rotated_vertices[i].x + CANVAS_WIDTH / 2);
        projected_points[i].y = (int16_t)(rotated_vertices[i].y + CANVAS_HEIGHT / 2);
    }

    const int edges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0}, // Bottom face
        {4, 5}, {5, 6}, {6, 7}, {7, 4}, // Top face
        {0, 4}, {1, 5}, {2, 6}, {3, 7}  // Side edges
    };

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_color_hex(0x9B59B6);
    line_dsc.width = 2;
    line_dsc.round_start = 1;
    line_dsc.round_end = 1;

    for (int i = 0; i < 12; i++) {
        lv_point_t line_points[] = {projected_points[edges[i][0]], projected_points[edges[i][1]]};
        lv_canvas_draw_line(canvas, line_points, 2, &line_dsc);
    }
}

// Create gyroscope test interface
static void create_gyroscope_test(lv_obj_t* content_container) {
    if (!content_container)
        return;

    lv_obj_clean(content_container);

    gyro_test_data_t* test_data = lv_mem_alloc(sizeof(gyro_test_data_t));
    if (!test_data) {
        ESP_LOGE(TAG, "Failed to allocate memory for gyro test data");
        return;
    }
    lv_obj_set_user_data(content_container, test_data);

    // Create a background container
    lv_obj_t* cube_area = lv_obj_create(content_container);
    lv_obj_set_size(cube_area, CANVAS_WIDTH + 20, CANVAS_HEIGHT + 20);
    lv_obj_align(cube_area, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_bg_color(cube_area, lv_color_hex(0x34495E), 0);
    lv_obj_set_style_bg_opa(cube_area, LV_OPA_50, 0);
    lv_obj_set_style_radius(cube_area, 8, 0);
    lv_obj_set_style_border_width(cube_area, 0, 0);

    // Create canvas
    test_data->canvas = lv_canvas_create(cube_area);
    lv_canvas_set_buffer(test_data->canvas, canvas_buf, CANVAS_WIDTH, CANVAS_HEIGHT, LV_IMG_CF_TRUE_COLOR);
    lv_obj_center(test_data->canvas);

    // Initialize cube vertices
    float size = 30.0f;
    point3d_t v[8] = {{-size, -size, -size}, {size, -size, -size}, {size, size, -size}, {-size, size, -size},
                      {-size, -size, size},  {size, -size, size},  {size, size, size},  {-size, size, size}};
    memcpy(test_data->initial_vertices, v, sizeof(v));

    // Initialize angles
    test_data->angle_x = 0.0f;
    test_data->angle_y = 0.0f;
    test_data->angle_z = 0.0f;

    // Draw initial state cube
    draw_cube_on_canvas(test_data->canvas, test_data->initial_vertices, test_data->angle_x, test_data->angle_y,
                        test_data->angle_z);

    // Create value display label
    test_data->value_label = lv_label_create(content_container);
    lv_label_set_text(test_data->value_label, "X: 0.00, Y: 0.00, Z: 0.00");
    lv_obj_align(test_data->value_label, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_obj_set_style_text_font(test_data->value_label, &lv_font_montserrat_14, 0);
    lv_obj_set_user_data(content_container, test_data); // Update user_data

    // Create button container
    lv_obj_t* btn_cont = lv_obj_create(content_container);
    lv_obj_set_size(btn_cont, 200, 50);
    lv_obj_align(btn_cont, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_bg_opa(btn_cont, LV_OPA_0, 0);
    lv_obj_set_style_border_width(btn_cont, 0, 0);
    lv_obj_set_style_pad_all(btn_cont, 0, 0);

    // Calibrate button
    lv_obj_t* calibrate_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(calibrate_btn, 80, 40);
    lv_obj_align(calibrate_btn, LV_ALIGN_LEFT_MID, 10, 0);
    theme_apply_to_button(calibrate_btn, true);
    lv_obj_add_event_cb(calibrate_btn, calibrate_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* calibrate_label = lv_label_create(calibrate_btn);
    lv_label_set_text(calibrate_label, "Calibrate");
    lv_obj_center(calibrate_label);

    // Test button
    lv_obj_t* test_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(test_btn, 80, 40);
    lv_obj_align(test_btn, LV_ALIGN_RIGHT_MID, -10, 0);
    theme_apply_to_button(test_btn, true);
    lv_obj_add_event_cb(test_btn, test_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* test_label = lv_label_create(test_btn);
    lv_label_set_text(test_label, "Start Test");
    lv_obj_center(test_label);

    ESP_LOGI(TAG, "Gyroscope test interface created");
}



// Test task implementation
static void test_task(void* pvParameter);

static void ui_update_timer_cb(lv_timer_t* timer) {
    QueueHandle_t test_queue = (QueueHandle_t)timer->user_data;
    test_msg_t msg;

    switch (g_current_state) {
    case CALIBRATION_STATE_JOYSTICK_TEST: {
        joystick_test_data_t* test_data = (joystick_test_data_t*)lv_obj_get_user_data(g_content_container);
        if (test_data) {
            if (xQueueReceive(test_queue, &msg, 0) == pdPASS) {
                if (msg.type == MSG_UPDATE_JOYSTICK) {
                    // Map joystick values to indicator position
                    // Assume joystick value range is -1000 to 1000
                    // Joystick area size is 120x120, center is (60, 60)
                    // Indicator movement range is -50 to 50
                    int16_t indicator_x = (int16_t)lv_map(msg.data.joystick.joy1_x, -1000, 1000, -50, 50);
                    int16_t indicator_y = (int16_t)lv_map(msg.data.joystick.joy1_y, -1000, 1000, -50, 50);
                    lv_obj_set_pos(test_data->joystick_indicator, 50 + indicator_x, 50 - indicator_y); // Y axis reversed

                    lv_label_set_text_fmt(test_data->value_label, "X: %d  Y: %d", msg.data.joystick.joy1_x, msg.data.joystick.joy1_y);
                }
            }
        }
        break;
    }
    case CALIBRATION_STATE_GYROSCOPE_TEST: {
        gyro_test_data_t* test_data = (gyro_test_data_t*)lv_obj_get_user_data(g_content_container);
        if (test_data) {
            // Process all queue messages
            while (xQueueReceive(test_queue, &msg, 0) == pdPASS) {
                if (msg.type == MSG_UPDATE_EULER) {
                    // [Critical] Use Euler angles from background Fusion AHRS, set directly (no integration drift)
                    // [Coordinate axis mapping correction] Adjust display according to actual hardware installation direction
                    // Swap Roll and Pitch, Yaw reversed
                    test_data->angle_x = msg.data.euler.pitch * (M_PI / 180.0f);   // X axis displays Pitch
                    test_data->angle_y = msg.data.euler.roll * (M_PI / 180.0f);    // Y axis displays Roll
                    test_data->angle_z = -msg.data.euler.yaw * (M_PI / 180.0f);    // Z axis displays -Yaw (reversed)

                    draw_cube_on_canvas(test_data->canvas, test_data->initial_vertices, 
                                        test_data->angle_x, test_data->angle_y, test_data->angle_z);
                }
                else if (msg.type == MSG_UPDATE_GYROSCOPE) {
                    // Update raw angular velocity numerical display
                    lv_label_set_text_fmt(test_data->value_label, "X: %.2f, Y: %.2f, Z: %.2f", 
                                          msg.data.gyro.x, msg.data.gyro.y, msg.data.gyro.z);
                }
            }
        }
        break;
    }

    default:
        break;
    }
}

static void test_task(void* pvParameter) {
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(20); // Update every 20ms (50Hz, increase sampling rate)
    test_msg_t msg;

    ESP_LOGI(TAG, "Test task started");

    while (1) {
        // Check if need to stop
        if (xQueueReceive(g_test_queue, &msg, 0) == pdTRUE) {
            if (msg.type == MSG_STOP_TEST) {
                ESP_LOGI(TAG, "Test task stopping...");
                break;
            }
        }

        if (g_test_running) {
            switch (g_current_state) {
            case CALIBRATION_STATE_JOYSTICK_TEST: {
                joystick_data_t joystick_data;
                if (joystick_adc_read(&joystick_data) == ESP_OK) {
                    test_msg_t update_msg;
                    update_msg.type = MSG_UPDATE_JOYSTICK;

                    // Only use joystick 1 data
                    update_msg.data.joystick.joy1_x = joystick_data.norm_joy1_x;
                    update_msg.data.joystick.joy1_y = joystick_data.norm_joy1_y;

                    xQueueSend(g_test_queue, &update_msg, 0);
                }
                break;
            }
            case CALIBRATION_STATE_GYROSCOPE_TEST: {
                // [Plan: Use results from background Fusion AHRS]
                // Background task is already running Fusion AHRS, we only read results to avoid conflicts
                attitude_data_t attitude;
                lsm6ds_control_get_attitude(&attitude);  // Protected by mutex
                
                test_msg_t euler_msg;
                euler_msg.type = MSG_UPDATE_EULER;
                euler_msg.data.euler.roll = attitude.roll;
                euler_msg.data.euler.pitch = attitude.pitch;
                euler_msg.data.euler.yaw = attitude.yaw;
                xQueueSend(g_test_queue, &euler_msg, 0);
                
                // Simultaneously read raw angular velocity for numerical display
                lsm6ds3_data_t imu_data;
                if (lsm6ds3_read_all(&imu_data) == ESP_OK) {
                    apply_gyroscope_calibration(&imu_data.gyro.x, &imu_data.gyro.y, &imu_data.gyro.z);
                    
                    test_msg_t gyro_msg;
                    gyro_msg.type = MSG_UPDATE_GYROSCOPE;
                    gyro_msg.data.gyro.x = imu_data.gyro.x;
                    gyro_msg.data.gyro.y = imu_data.gyro.y;
                    gyro_msg.data.gyro.z = imu_data.gyro.z;
                    xQueueSend(g_test_queue, &gyro_msg, 0);
                }
                break;
            }

            default:
                break;
            }
        }

        vTaskDelayUntil(&last_wake_time, frequency);
    }

    ESP_LOGI(TAG, "Test task stopped");
    g_test_task_handle = NULL;
    vTaskDelete(NULL);
}

// Update joystick test interface
static void update_joystick_test_ui(int16_t joy1_x, int16_t joy1_y) {
    if (!g_content_container)
        return;

    joystick_test_data_t* test_data = (joystick_test_data_t*)lv_obj_get_user_data(g_content_container);
    if (!test_data)
        return;

    // Update joystick indicator position (-100 to 100 mapped to circular area)
    int16_t joy_pos_x = (joy1_x * 45) / 100;  // 45 is 75% of circle radius
    int16_t joy_pos_y = -(joy1_y * 45) / 100; // Y axis reversed
    lv_obj_align(test_data->joystick_indicator, LV_ALIGN_CENTER, joy_pos_x, joy_pos_y);

    // Update numerical display
    char text_buf[32];
    snprintf(text_buf, sizeof(text_buf), "X: %d  Y: %d", joy1_x, joy1_y);
    lv_label_set_text(test_data->value_label, text_buf);
}




