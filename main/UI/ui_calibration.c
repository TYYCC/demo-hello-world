/**
 * @file ui_calibration.c
 * @brief 校准和测试界面 - 支持各种外设的校准和测试
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
#include "lsm6ds_control.h"  // 后台 Fusion AHRS 任务
#include "my_font.h"
#include "theme_manager.h"
#include "ui.h"

static const char* TAG = "UI_CALIBRATION";

// 画布定义
#define CANVAS_WIDTH 120
#define CANVAS_HEIGHT 120
static lv_color_t* canvas_buf = NULL;

// 界面状态
typedef enum {
    CALIBRATION_STATE_MAIN_MENU,
    CALIBRATION_STATE_JOYSTICK_TEST,
    CALIBRATION_STATE_GYROSCOPE_TEST,
    CALIBRATION_STATE_ACCELEROMETER_TEST,
    CALIBRATION_STATE_TOUCHSCREEN_TEST
} calibration_state_t;

// 前向声明
static void create_main_menu(lv_obj_t* content_container);
static void create_joystick_test(lv_obj_t* content_container);
static void create_gyroscope_test(lv_obj_t* content_container);
static void create_accelerometer_test(lv_obj_t* content_container);
static void test_task(void* pvParameter);
static void ui_update_timer_cb(lv_timer_t* timer);

// 全局变量
static lv_obj_t* g_page_parent_container = NULL;
static lv_obj_t* g_content_container = NULL;
static lv_obj_t* g_info_label = NULL;
static lv_obj_t* g_calibrate_btn = NULL;
static lv_obj_t* g_test_btn = NULL;

// 当前状态
static calibration_state_t g_current_state = CALIBRATION_STATE_MAIN_MENU;
static bool g_test_running = false;
static TaskHandle_t g_test_task_handle = NULL;
static lv_timer_t* g_ui_update_timer = NULL;
static QueueHandle_t g_test_queue = NULL;

// 消息类型
typedef enum { 
    MSG_UPDATE_JOYSTICK, 
    MSG_UPDATE_GYROSCOPE,      // 陀螺仪角速度数据（用于数值显示）
    MSG_UPDATE_EULER,          // 后台 Fusion AHRS 的欧拉角（用于立方体显示）
    MSG_UPDATE_ACCELEROMETER, 
    MSG_STOP_TEST 
} test_msg_type_t;

// 摇杆测试数据结构
typedef struct {
    lv_obj_t* joystick_indicator;
    lv_obj_t* value_label;
} joystick_test_data_t;

// 3D点结构体
typedef struct {
    float x, y, z;
} point3d_t;

// 陀螺仪测试数据结构
typedef struct {
    lv_obj_t* canvas;
    lv_obj_t* value_label;
    point3d_t initial_vertices[8];   // 初始立方体顶点
    float angle_x, angle_y, angle_z; // 旋转角度（使用改进的积分算法）
} gyro_test_data_t;

typedef struct {
    test_msg_type_t type;
    union {
        struct {
            int16_t joy1_x, joy1_y;
        } joystick;
        struct {
            float x, y, z;  // 角速度 (dps)
        } gyro;
        struct {
            float roll, pitch, yaw;  // 欧拉角（度）- 从后台 Fusion AHRS 读取
        } euler;
        struct {
            float x, y, z;
        } accel;
    } data;
} test_msg_t;

// 自定义返回按钮回调 - 处理校准界面的特殊逻辑
static void calibration_back_btn_callback(lv_event_t* e) {
    if (g_test_running) {
        // 停止测试
        test_msg_t msg = {.type = MSG_STOP_TEST};
        xQueueSend(g_test_queue, &msg, pdMS_TO_TICKS(100));
        g_test_running = false;

        // 删除UI更新定时器
        if (g_ui_update_timer) {
            lv_timer_del(g_ui_update_timer);
            g_ui_update_timer = NULL;
        }
    }

    if (g_current_state == CALIBRATION_STATE_MAIN_MENU) {
        // 返回主菜单
        lv_obj_t* screen = lv_scr_act();
        if (screen) {
            lv_obj_clean(screen);
            ui_main_menu_create(screen);
        }
    } else {
        // 返回校准主菜单，刷新显示最新状态
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
    case CALIBRATION_STATE_ACCELEROMETER_TEST:
        ret = calibrate_accelerometer();
        break;
    default:
        break;
    }

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration completed successfully");
        
        // 【重要】校准完成后，立即更新主菜单显示
        // 无论当前在哪个状态，都刷新一次确保状态同步
        const calibration_status_t* status = get_calibration_status();
        if (status) {
            ESP_LOGI(TAG, "Updated calibration status: Gyro=%d, Accel=%d", 
                     status->gyroscope_calibrated, status->accelerometer_calibrated);
        }
        
        // 如果在主菜单，刷新显示
        if (g_current_state == CALIBRATION_STATE_MAIN_MENU) {
            create_main_menu(g_content_container);
        }
    } else {
        ESP_LOGE(TAG, "Calibration failed: %s", esp_err_to_name(ret));
    }
}

static void test_btn_event_cb(lv_event_t* e) {
    if (g_test_running) {
        // 停止测试
        g_test_running = false;

        // 发送停止消息到测试任务
        test_msg_t msg = {.type = MSG_STOP_TEST};
        xQueueSend(g_test_queue, &msg, pdMS_TO_TICKS(100));

        // 删除UI更新定时器
        if (g_ui_update_timer) {
            lv_timer_del(g_ui_update_timer);
            g_ui_update_timer = NULL;
        }

        lv_obj_t* btn = lv_event_get_target(e);
        lv_obj_t* label = lv_obj_get_child(btn, 0);
        lv_label_set_text(label, "Start Test");

        ESP_LOGI(TAG, "Test stopped");
    } else {
        // 开始测试
        g_test_running = true;

        // 启动测试任务
        if (g_test_task_handle == NULL) {
            BaseType_t ret = xTaskCreate(test_task, "test_task", 4096, NULL, 5, &g_test_task_handle);
            if (ret != pdPASS) {
                ESP_LOGE(TAG, "Failed to create test task");
                g_test_running = false;
                return;
            }
        }

        // 创建UI更新定时器（20ms = 50Hz，提高响应速度）
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
    case 2: // Accelerometer Test
        g_current_state = CALIBRATION_STATE_ACCELEROMETER_TEST;
        create_accelerometer_test(g_content_container);
        break;
    case 3: // Touchscreen Test
        g_current_state = CALIBRATION_STATE_TOUCHSCREEN_TEST;
        // TODO: 实现触摸屏测试
        break;
    }
}

// 创建主菜单
static void create_main_menu(lv_obj_t* content_container) {
    if (!content_container)
        return;

    lv_obj_clean(content_container);

    // 【关键】获取最新的校准状态
    // 确保显示的是从 NVS 加载的实际状态
    const calibration_status_t* status = get_calibration_status();
    if (status) {
        ESP_LOGI(TAG, "Displaying calibration status: Gyro=%d, Accel=%d", 
                 status->gyroscope_calibrated, status->accelerometer_calibrated);
        char status_text[256];
        snprintf(status_text, sizeof(status_text),
                 "校准状态:\n"
                 "摇杆: %s\n"
                 "陀螺仪: %s\n"
                 "加速度计: %s\n"
                 "电池: %s\n"
                 "触摸屏: %s",
                 status->joystick_calibrated ? "已校准" : "未校准", status->gyroscope_calibrated ? "已校准" : "未校准",
                 status->accelerometer_calibrated ? "已校准" : "未校准",
                 status->battery_calibrated ? "已校准" : "未校准",
                 status->touchscreen_calibrated ? "已校准" : "未校准");

        g_info_label = lv_label_create(content_container);
        lv_label_set_text(g_info_label, status_text);
        theme_apply_to_label(g_info_label, false);
        lv_obj_align(g_info_label, LV_ALIGN_TOP_MID, 0, 10);
        lv_font_t* loaded_font = get_loaded_font();
        lv_obj_set_style_text_font(g_info_label, loaded_font, 0);
    }

    // 创建菜单按钮
    const char* menu_items[] = {"Joystick Test", "Gyroscope Test", "Accelerometer Test", "Touchscreen Test"};

    for (int i = 0; i < 4; i++) {
        lv_obj_t* btn = lv_btn_create(content_container);
        lv_obj_set_size(btn, 200, 40);
        lv_obj_align(btn, LV_ALIGN_CENTER, 0, 60 + i * 50);
        theme_apply_to_button(btn, true);

        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, menu_items[i]);
        lv_obj_center(label);

        // 设置按钮事件
        lv_obj_set_user_data(btn, (void*)(intptr_t)i);
        lv_obj_add_event_cb(btn, menu_btn_event_cb, LV_EVENT_CLICKED, NULL);
    }
}

// 创建校准界面
void ui_calibration_create(lv_obj_t* parent) {
    g_current_state = CALIBRATION_STATE_MAIN_MENU;

    // 从PSRAM分配画布缓冲区
    canvas_buf = heap_caps_malloc(LV_CANVAS_BUF_SIZE_TRUE_COLOR(CANVAS_WIDTH, CANVAS_HEIGHT), MALLOC_CAP_SPIRAM);
    if (!canvas_buf) {
        ESP_LOGE(TAG, "Failed to allocate canvas buffer in PSRAM");
        return;
    }

    // 创建消息队列
    g_test_queue = xQueueCreate(10, sizeof(test_msg_t));
    if (!g_test_queue) {
        ESP_LOGE(TAG, "Failed to create test queue");
        heap_caps_free(canvas_buf); // 如果队列创建失败，则释放缓冲区
        canvas_buf = NULL;
        return;
    }

    // 应用当前主题到屏幕
    theme_apply_to_screen(parent);

    // 1. 创建页面父级容器（统一管理整个页面）
    ui_create_page_parent_container(parent, &g_page_parent_container);

    // 2. 创建顶部栏容器（包含返回按钮和标题）
    lv_obj_t* top_bar_container;
    lv_obj_t* title_container;
    ui_create_top_bar(g_page_parent_container, "Calibration & Test", false, &top_bar_container, &title_container, NULL);

    // 替换顶部栏的返回按钮回调为自定义回调
    lv_obj_t* back_btn = lv_obj_get_child(top_bar_container, 0); // 获取返回按钮
    if (back_btn) {
        lv_obj_remove_event_cb(back_btn, NULL); // 移除默认回调
        lv_obj_add_event_cb(back_btn, calibration_back_btn_callback, LV_EVENT_CLICKED, NULL);
    }

    // 3. 创建页面内容容器
    ui_create_page_content_area(g_page_parent_container, &g_content_container);

    // 4. 创建主菜单
    create_main_menu(g_content_container);

    // 5. 创建底部按钮容器
    lv_obj_t* btn_cont = lv_obj_create(g_page_parent_container);
    lv_obj_set_size(btn_cont, 240, 50);
    lv_obj_align(btn_cont, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_bg_opa(btn_cont, LV_OPA_0, 0);
    lv_obj_set_style_border_width(btn_cont, 0, 0);
    lv_obj_set_style_pad_all(btn_cont, 0, 0);

    // 校准按钮
    g_calibrate_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(g_calibrate_btn, 80, 40);
    lv_obj_align(g_calibrate_btn, LV_ALIGN_LEFT_MID, 10, 0);
    theme_apply_to_button(g_calibrate_btn, true);
    lv_obj_add_event_cb(g_calibrate_btn, calibrate_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* calibrate_label = lv_label_create(g_calibrate_btn);
    lv_label_set_text(calibrate_label, "Calibrate");
    lv_obj_center(calibrate_label);

    // 测试按钮
    g_test_btn = lv_btn_create(btn_cont);
    lv_obj_set_size(g_test_btn, 80, 40);
    lv_obj_align(g_test_btn, LV_ALIGN_RIGHT_MID, -10, 0);
    theme_apply_to_button(g_test_btn, true);
    lv_obj_add_event_cb(g_test_btn, test_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* test_label = lv_label_create(g_test_btn);
    lv_label_set_text(test_label, "Start Test");
    lv_obj_center(test_label);

    ESP_LOGI(TAG, "Calibration UI created successfully");
}

// 销毁校准界面
void ui_calibration_destroy(void) {
    // 停止测试
    if (g_test_running) {
        test_msg_t msg = {.type = MSG_STOP_TEST};
        xQueueSend(g_test_queue, &msg, pdMS_TO_TICKS(100));
        g_test_running = false;
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // 删除测试任务
    if (g_test_task_handle) {
        vTaskDelete(g_test_task_handle);
        g_test_task_handle = NULL;
    }

    // 删除UI更新定时器
    if (g_ui_update_timer) {
        lv_timer_del(g_ui_update_timer);
        g_ui_update_timer = NULL;
    }

    // 释放摇杆测试数据内存
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

    // 删除消息队列
    if (g_test_queue) {
        vQueueDelete(g_test_queue);
        g_test_queue = NULL;
    }

    // 释放画布缓冲区
    if (canvas_buf) {
        heap_caps_free(canvas_buf);
        canvas_buf = NULL;
    }

    // 清空全局变量
    g_page_parent_container = NULL;
    g_content_container = NULL;
    g_info_label = NULL;
    g_calibrate_btn = NULL;
    g_test_btn = NULL;

    ESP_LOGI(TAG, "Calibration UI destroyed");
}

// 创建摇杆测试界面
static void create_joystick_test(lv_obj_t* content_container) {
    if (!content_container)
        return;

    lv_obj_clean(content_container);

    // 创建摇杆显示区域
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

    // 创建摇杆标签
    lv_obj_t* joy_label = lv_label_create(content_container);
    lv_label_set_text(joy_label, "Joystick");
    lv_obj_align(joy_label, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_text_font(joy_label, &lv_font_montserrat_14, 0);

    // 创建数值显示标签
    lv_obj_t* value_label = lv_label_create(content_container);
    lv_label_set_text(value_label, "X: 0  Y: 0");
    lv_obj_align(value_label, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_CENTER, 0);

    // 保存控件引用用于更新
    joystick_test_data_t* test_data = lv_mem_alloc(sizeof(joystick_test_data_t));
    test_data->joystick_indicator = joystick_indicator;
    test_data->value_label = value_label;
    lv_obj_set_user_data(content_container, test_data);

    ESP_LOGI(TAG, "Joystick test interface created");
}

// 绘制立方体到画布
static void draw_cube_on_canvas(lv_obj_t* canvas, point3d_t* initial_vertices, float angle_x, float angle_y,
                                float angle_z) {
    lv_canvas_fill_bg(canvas, lv_color_hex(0x34495E), LV_OPA_COVER);

    point3d_t rotated_vertices[8];
    // 旋转矩阵计算
    float sin_ax = sinf(angle_x), cos_ax = cosf(angle_x);
    float sin_ay = sinf(angle_y), cos_ay = cosf(angle_y);
    float sin_az = sinf(angle_z), cos_az = cosf(angle_z);

    for (int i = 0; i < 8; i++) {
        point3d_t p = initial_vertices[i];

        // 绕X轴旋转
        float y = p.y * cos_ax - p.z * sin_ax;
        float z = p.y * sin_ax + p.z * cos_ax;
        p.y = y;
        p.z = z;

        // 绕Y轴旋转
        float x = p.x * cos_ay + p.z * sin_ay;
        z = -p.x * sin_ay + p.z * cos_ay;
        p.x = x;
        p.z = z;

        // 绕Z轴旋转
        x = p.x * cos_az - p.y * sin_az;
        y = p.x * sin_az + p.y * cos_az;
        p.x = x;
        p.y = y;

        rotated_vertices[i] = p;
    }

    // 投影并绘制
    lv_point_t projected_points[8];
    for (int i = 0; i < 8; i++) {
        // 正交投影
        projected_points[i].x = (int16_t)(rotated_vertices[i].x + CANVAS_WIDTH / 2);
        projected_points[i].y = (int16_t)(rotated_vertices[i].y + CANVAS_HEIGHT / 2);
    }

    const int edges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0}, // 底面
        {4, 5}, {5, 6}, {6, 7}, {7, 4}, // 顶面
        {0, 4}, {1, 5}, {2, 6}, {3, 7}  // 侧边
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

// 创建陀螺仪测试界面
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

    // 创建一个背景容器
    lv_obj_t* cube_area = lv_obj_create(content_container);
    lv_obj_set_size(cube_area, CANVAS_WIDTH + 20, CANVAS_HEIGHT + 20);
    lv_obj_align(cube_area, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_bg_color(cube_area, lv_color_hex(0x34495E), 0);
    lv_obj_set_style_bg_opa(cube_area, LV_OPA_50, 0);
    lv_obj_set_style_radius(cube_area, 8, 0);
    lv_obj_set_style_border_width(cube_area, 0, 0);

    // 创建画布
    test_data->canvas = lv_canvas_create(cube_area);
    lv_canvas_set_buffer(test_data->canvas, canvas_buf, CANVAS_WIDTH, CANVAS_HEIGHT, LV_IMG_CF_TRUE_COLOR);
    lv_obj_center(test_data->canvas);

    // 初始化立方体顶点
    float size = 30.0f;
    point3d_t v[8] = {{-size, -size, -size}, {size, -size, -size}, {size, size, -size}, {-size, size, -size},
                      {-size, -size, size},  {size, -size, size},  {size, size, size},  {-size, size, size}};
    memcpy(test_data->initial_vertices, v, sizeof(v));

    // 初始化角度
    test_data->angle_x = 0.0f;
    test_data->angle_y = 0.0f;
    test_data->angle_z = 0.0f;

    // 绘制初始状态的立方体
    draw_cube_on_canvas(test_data->canvas, test_data->initial_vertices, test_data->angle_x, test_data->angle_y,
                        test_data->angle_z);

    // 创建数值显示标签
    test_data->value_label = lv_label_create(content_container);
    lv_label_set_text(test_data->value_label, "X: 0.00, Y: 0.00, Z: 0.00");
    lv_obj_align(test_data->value_label, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_obj_set_style_text_font(test_data->value_label, &lv_font_montserrat_14, 0);
    lv_obj_set_user_data(content_container, test_data); // 更新user_data

    ESP_LOGI(TAG, "Gyroscope test interface created");
}

// 创建加速度计测试界面
static void create_accelerometer_test(lv_obj_t* content_container) {
    if (!content_container)
        return;

    lv_obj_clean(content_container);

    // 创建重力指示器
    lv_obj_t* gravity_area = lv_obj_create(content_container);
    lv_obj_set_size(gravity_area, 200, 200);
    lv_obj_align(gravity_area, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_bg_color(gravity_area, lv_color_hex(0x34495E), 0);
    lv_obj_set_style_bg_opa(gravity_area, LV_OPA_50, 0);
    lv_obj_set_style_radius(gravity_area, 100, 0);
    lv_obj_set_style_border_width(gravity_area, 2, 0);
    lv_obj_set_style_border_color(gravity_area, lv_color_hex(0x95A5A6), 0);

    // 创建重力指示器
    lv_obj_t* gravity_indicator = lv_obj_create(gravity_area);
    lv_obj_set_size(gravity_indicator, 30, 30);
    lv_obj_align(gravity_indicator, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(gravity_indicator, lv_color_hex(0xF39C12), 0);
    lv_obj_set_style_radius(gravity_indicator, 15, 0);
    lv_obj_set_user_data(gravity_area, gravity_indicator);

    // 创建数值显示标签
    lv_obj_t* value_label = lv_label_create(content_container);
    lv_label_set_text(value_label, "X: 0.00, Y: 0.00, Z: 0.00");
    lv_obj_align(value_label, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_14, 0);
    lv_obj_set_user_data(content_container, value_label);

    ESP_LOGI(TAG, "Accelerometer test interface created");
}

// 测试任务实现
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
                    // 将摇杆值映射到指示器位置
                    // 假设摇杆值范围是 -1000 到 1000
                    // 摇杆区域大小是 120x120，中心是 (60, 60)
                    // 指示器移动范围是 -50 到 50
                    int16_t indicator_x = (int16_t)lv_map(msg.data.joystick.joy1_x, -1000, 1000, -50, 50);
                    int16_t indicator_y = (int16_t)lv_map(msg.data.joystick.joy1_y, -1000, 1000, -50, 50);
                    lv_obj_set_pos(test_data->joystick_indicator, 50 + indicator_x, 50 - indicator_y); // Y轴反向

                    lv_label_set_text_fmt(test_data->value_label, "X: %d  Y: %d", msg.data.joystick.joy1_x, msg.data.joystick.joy1_y);
                }
            }
        }
        break;
    }
    case CALIBRATION_STATE_GYROSCOPE_TEST: {
        gyro_test_data_t* test_data = (gyro_test_data_t*)lv_obj_get_user_data(g_content_container);
        if (test_data) {
            // 处理所有队列消息
            while (xQueueReceive(test_queue, &msg, 0) == pdPASS) {
                if (msg.type == MSG_UPDATE_EULER) {
                    // 【关键】使用后台 Fusion AHRS 的欧拉角，直接设置（无积分漂移）
                    // 后台任务以 10Hz 更新，已经融合了加速度计和陀螺仪
                    test_data->angle_x = msg.data.euler.roll * (M_PI / 180.0f);   // Roll
                    test_data->angle_y = msg.data.euler.pitch * (M_PI / 180.0f);  // Pitch
                    test_data->angle_z = msg.data.euler.yaw * (M_PI / 180.0f);    // Yaw

                    draw_cube_on_canvas(test_data->canvas, test_data->initial_vertices, 
                                        test_data->angle_x, test_data->angle_y, test_data->angle_z);
                }
                else if (msg.type == MSG_UPDATE_GYROSCOPE) {
                    // 更新原始角速度数值显示
                    lv_label_set_text_fmt(test_data->value_label, "X: %.2f, Y: %.2f, Z: %.2f", 
                                          msg.data.gyro.x, msg.data.gyro.y, msg.data.gyro.z);
                }
            }
        }
        break;
    }
    case CALIBRATION_STATE_ACCELEROMETER_TEST: {
        lv_obj_t* gravity_indicator = (lv_obj_t*)lv_obj_get_user_data(lv_obj_get_child(g_content_container, 0));
        lv_obj_t* value_label = (lv_obj_t*)lv_obj_get_user_data(g_content_container);
        if (gravity_indicator && value_label) {
            if (xQueueReceive(test_queue, &msg, 0) == pdPASS) {
                if (msg.type == MSG_UPDATE_ACCELEROMETER) {
                    // 将加速度计值映射到指示器位置
                    // 假设加速度计值范围是 -1到1G
                    // 区域大小是 200x200，中心是 (100, 100)
                    // 指示器移动范围是 -80 到 80
                    int16_t indicator_x = (int16_t)lv_map((int32_t)(msg.data.accel.x * 100), -100, 100, -80, 80);
                    int16_t indicator_y = (int16_t)lv_map((int32_t)(msg.data.accel.y * 100), -100, 100, -80, 80);
                    lv_obj_set_pos(gravity_indicator, 85 + indicator_x, 85 - indicator_y); // Y轴反向

                    lv_label_set_text_fmt(value_label, "X: %.2f, Y: %.2f, Z: %.2f", msg.data.accel.x, msg.data.accel.y,
                                        msg.data.accel.z);
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
    const TickType_t frequency = pdMS_TO_TICKS(20); // 20ms更新一次 (50Hz，提高采样率)
    test_msg_t msg;

    ESP_LOGI(TAG, "Test task started");

    while (1) {
        // 检查是否需要停止
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

                    // 只使用摇杆1的数据
                    update_msg.data.joystick.joy1_x = joystick_data.norm_joy1_x;
                    update_msg.data.joystick.joy1_y = joystick_data.norm_joy1_y;

                    xQueueSend(g_test_queue, &update_msg, 0);
                }
                break;
            }
            case CALIBRATION_STATE_GYROSCOPE_TEST: {
                // 【方案：使用后台 Fusion AHRS 的结果】
                // 后台任务已经在运行 Fusion AHRS，我们只读取结果，避免冲突
                attitude_data_t attitude;
                lsm6ds_control_get_attitude(&attitude);  // 带互斥锁保护
                
                test_msg_t euler_msg;
                euler_msg.type = MSG_UPDATE_EULER;
                euler_msg.data.euler.roll = attitude.roll;
                euler_msg.data.euler.pitch = attitude.pitch;
                euler_msg.data.euler.yaw = attitude.yaw;
                xQueueSend(g_test_queue, &euler_msg, 0);
                
                // 同时读取原始角速度用于数值显示
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
            case CALIBRATION_STATE_ACCELEROMETER_TEST: {
                lsm6ds3_data_t imu_data;
                if (lsm6ds3_read_all(&imu_data) == ESP_OK) {
                    // 【关键】应用用户校准的偏置
                    apply_accelerometer_calibration(&imu_data.accel.x, &imu_data.accel.y, &imu_data.accel.z);
                    
                    test_msg_t update_msg;
                    update_msg.type = MSG_UPDATE_ACCELEROMETER;
                    update_msg.data.accel.x = imu_data.accel.x;
                    update_msg.data.accel.y = imu_data.accel.y;
                    update_msg.data.accel.z = imu_data.accel.z;
                    xQueueSend(g_test_queue, &update_msg, 0);
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

// 更新摇杆测试界面
static void update_joystick_test_ui(int16_t joy1_x, int16_t joy1_y) {
    if (!g_content_container)
        return;

    joystick_test_data_t* test_data = (joystick_test_data_t*)lv_obj_get_user_data(g_content_container);
    if (!test_data)
        return;

    // 更新摇杆指示器位置 (-100 到 100 映射到圆形区域)
    int16_t joy_pos_x = (joy1_x * 45) / 100;  // 45是圆形半径的75%
    int16_t joy_pos_y = -(joy1_y * 45) / 100; // Y轴反向
    lv_obj_align(test_data->joystick_indicator, LV_ALIGN_CENTER, joy_pos_x, joy_pos_y);

    // 更新数值显示
    char text_buf[32];
    snprintf(text_buf, sizeof(text_buf), "X: %d  Y: %d", joy1_x, joy1_y);
    lv_label_set_text(test_data->value_label, text_buf);
}

// 注意：update_gyroscope_test_ui 已删除
// 现在使用 Fusion AHRS 的欧拉角，在 ui_update_timer_cb 中直接处理

// 更新加速度计测试界面
static void update_accelerometer_test_ui(float accel_x, float accel_y, float accel_z) {
    if (!g_content_container)
        return;

    lv_obj_t* value_label = (lv_obj_t*)lv_obj_get_user_data(g_content_container);
    if (!value_label)
        return;

    char text_buf[64];
    snprintf(text_buf, sizeof(text_buf), "X: %.2f, Y: %.2f, Z: %.2f", accel_x, accel_y, accel_z);
    lv_label_set_text(value_label, text_buf);
}

// 注意：gyro_update_timer_cb 已被 ui_update_timer_cb 替代，功能合并
