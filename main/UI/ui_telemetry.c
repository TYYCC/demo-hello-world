#include "my_font.h" // 包含字体头文件
#include "lvgl.h"
#include "telemetry_main.h"           // 添加遥测服务头文件
#include "theme_manager.h"
#include "ui.h"
#include "ui_common.h"

// 语言文本定义
typedef struct {
    const char* telemetry_title;
    const char* throttle_label;
    const char* direction_label;
    const char* telemetry_status_label;
    const char* voltage_label;
    const char* current_label;
    const char* altitude_label;
    const char* gps_connected;
    const char* gps_searching;
    const char* gps_disconnected;
    const char* extended_functions;
    const char* start_button;
    const char* stop_button;
    const char* status_running;
    const char* status_stopped;
    const char* status_start_failed;
    const char* roll_prefix;
    const char* pitch_prefix;
    const char* yaw_prefix;
} telemetry_text_t;

// 英文文本
static const telemetry_text_t english_text = {
    .telemetry_title = "Remote Control",
    .throttle_label = "Throttle:",
    .direction_label = "Direction:",
    .telemetry_status_label = "Status",
    .voltage_label = "V: -- V",
    .current_label = "I: -- A",
    .altitude_label = "Altitude: -- m",
    .gps_connected = "GPS: Connected",
    .gps_searching = "GPS: Searching",
    .gps_disconnected = "GPS: Disconnected",
    .extended_functions = "Extended Functions",
    .start_button = "Start",
    .stop_button = "Stop",
    .status_running = "Status: Running",
    .status_stopped = "Status: Stopped",
    .status_start_failed = "Status: Start Failed",
    .roll_prefix = "R:",
    .pitch_prefix = "P:",
    .yaw_prefix = "Y:"
};

// 中文文本
static const telemetry_text_t chinese_text = {
    .telemetry_title = "遥控器",
    .throttle_label = "油门:",
    .direction_label = "方向:",
    .telemetry_status_label = "遥测状态",
    .voltage_label = "电压: -- V",
    .current_label = "电流: -- A",
    .altitude_label = "高度: -- m",
    .gps_connected = "GPS: 已连接",
    .gps_searching = "GPS: 搜索中",
    .gps_disconnected = "GPS: 未连接",
    .extended_functions = "扩展功能",
    .start_button = "启动",
    .stop_button = "停止",
    .status_running = "状态: 运行中",
    .status_stopped = "状态: 已停止",
    .status_start_failed = "状态: 启动失败",
    .roll_prefix = "R:",
    .pitch_prefix = "P:",
    .yaw_prefix = "Y:"
};

// 获取当前语言文本
static const telemetry_text_t* get_current_telemetry_text(void) {
    return (ui_get_current_language() == LANG_CHINESE) ? &chinese_text : &english_text;
}

// 获取当前字体
static const lv_font_t* get_current_telemetry_font(void) {
    if (ui_get_current_language() == LANG_CHINESE) {
        return get_loaded_font();
    }
    return &lv_font_montserrat_16;
}

// 设置语言显示
static void set_telemetry_language_display(lv_obj_t* obj) {
    const lv_font_t* font = get_current_telemetry_font();
    if (font) {
        lv_obj_set_style_text_font(obj, font, 0);
    }
}

// 遥测界面的全局变量
static lv_obj_t* throttle_slider;      // 油门滑动条
static lv_obj_t* direction_slider;     // 方向滑动条

// ELRS链路统计标签
static lv_obj_t* rssi_1_label;         // 天线1 RSSI
static lv_obj_t* rssi_2_label;         // 天线2 RSSI
static lv_obj_t* lq_label;             // 链路质量 LQ
static lv_obj_t* snr_label;            // 信噪比 SNR
static lv_obj_t* antenna_label;        // 天线选择
static lv_obj_t* model_match_label;    // 模型匹配

// 遥控通道显示 (16个通道)
static lv_obj_t* channel_labels[16];   // 通道标签数组

// 扩展遥测标签
static lv_obj_t* voltage_label;        // 电压标签
static lv_obj_t* current_label;        // 电流标签
static lv_obj_t* roll_label;           // 横滚角标签
static lv_obj_t* pitch_label;          // 俯仰角标签
static lv_obj_t* yaw_label;            // 偏航角标签
static lv_obj_t* altitude_label;       // 高度标签
static lv_obj_t* service_status_label; // 服务状态标签
static lv_obj_t* start_stop_btn;       // 启动/停止按钮

// 服务状态标志
static bool telemetry_service_active = false;
static lv_obj_t* gps_label;               // GPS状态标签
// static lv_timer_t* local_ui_update_timer; // 本地UI更新定时器 (已移除)

// 事件处理函数声明
static void slider_event_handler(lv_event_t* e);
static void settings_btn_event_handler(lv_event_t* e);
static void start_stop_btn_event_handler(lv_event_t* e);
static void telemetry_data_update_callback(const telemetry_data_t* data);

/**
 * @brief 创建遥测界面
 *
 * @param parent 父对象,为活动页面
 */
void ui_telemetry_create(lv_obj_t* parent) {
    theme_apply_to_screen(parent);

    // 获取当前语言文本
    const telemetry_text_t* text = get_current_telemetry_text();

    // 初始化遥测服务
    if (telemetry_service_init() != 0) {
        LV_LOG_ERROR("Failed to initialize telemetry service");
    }

    // 1. 创建顶部栏
    lv_obj_t* top_bar = NULL;
    lv_obj_t* title_container = NULL;
    lv_obj_t* settings_btn = NULL;
    ui_create_top_bar(parent, text->telemetry_title, true, &top_bar, &title_container, &settings_btn);

    // 为标题设置字体
    if (title_container) {
        lv_obj_t* title = lv_obj_get_child(title_container, 0);
        if (title) {
            set_telemetry_language_display(title);
        }
    }

    // 将右上角的设置按钮改为启动/停止按钮
    if (settings_btn) {
        start_stop_btn = settings_btn;

        lv_obj_remove_event_cb(start_stop_btn, NULL);

        lv_obj_t* btn_label = lv_label_create(start_stop_btn);
        lv_label_set_text(btn_label, text->start_button);
        set_telemetry_language_display(btn_label);
        lv_obj_set_style_text_color(btn_label, lv_color_white(), 0);
        lv_obj_center(btn_label);

        // 设置按钮样式为绿色
        lv_obj_set_style_bg_color(start_stop_btn, lv_color_hex(0x00AA00), 0);

        // 添加启动/停止事件回调
        lv_obj_add_event_cb(start_stop_btn, start_stop_btn_event_handler, LV_EVENT_CLICKED, NULL);
    }

    // 2. 创建内容容器
    lv_obj_t* content_container;
    ui_create_page_content_area(parent, &content_container);

    // 设置内容容器的布局，使其子控件垂直排列
    lv_obj_set_flex_flow(content_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(content_container, 5, 0);
    lv_obj_set_style_pad_gap(content_container, 10, 0);

    // 3. 在内容容器中创建控件
    // 油门/方向 和 遥测状态 区域 - 使用水平布局
    lv_obj_t* control_row = lv_obj_create(content_container);
    lv_obj_set_width(control_row, lv_pct(100));
    lv_obj_set_height(control_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(control_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(control_row, 5, 0);
    lv_obj_set_style_pad_gap(control_row, 10, 0);

    // 左侧容器：油门/方向
    lv_obj_t* left_container = lv_obj_create(control_row);
    lv_obj_set_width(left_container, lv_pct(48));
    lv_obj_set_height(left_container, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(left_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(left_container, 10, 0);
    lv_obj_set_style_pad_gap(left_container, 8, 0);

    // 油门控制
    lv_obj_t* throttle_label = lv_label_create(left_container);
    lv_label_set_text(throttle_label, text->throttle_label);
    set_telemetry_language_display(throttle_label);

    throttle_slider = lv_slider_create(left_container);
    lv_obj_set_size(throttle_slider, lv_pct(100), 7);
    lv_obj_set_style_pad_all(throttle_slider, 2, LV_PART_KNOB);
    lv_slider_set_range(throttle_slider, 0, 1000);
    lv_slider_set_value(throttle_slider, 500, LV_ANIM_OFF);
    lv_obj_add_event_cb(throttle_slider, slider_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

    // 方向控制
    lv_obj_t* direction_label = lv_label_create(left_container);
    lv_label_set_text(direction_label, text->direction_label);
    set_telemetry_language_display(direction_label);

    direction_slider = lv_slider_create(left_container);
    lv_obj_set_size(direction_slider, lv_pct(100), 7);
    lv_obj_set_style_pad_all(direction_slider, 2, LV_PART_KNOB);
    lv_slider_set_range(direction_slider, 0, 1000);
    lv_slider_set_value(direction_slider, 500, LV_ANIM_OFF);
    lv_obj_add_event_cb(direction_slider, slider_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

    // 右侧容器：ELRS链路统计信息
    lv_obj_t* right_container = lv_obj_create(control_row);
    lv_obj_set_width(right_container, lv_pct(48));
    lv_obj_set_height(right_container, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(right_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(right_container, 10, 0);
    lv_obj_set_style_pad_gap(right_container, 5, 0);

    // 右侧标题
    lv_obj_t* title2 = lv_label_create(right_container);
    lv_label_set_text(title2, "Link Stats");
    set_telemetry_language_display(title2);

    // RSSI 1显示
    rssi_1_label = lv_label_create(right_container);
    lv_label_set_text(rssi_1_label, "RSSI1: -- dBm");
    set_telemetry_language_display(rssi_1_label);

    // RSSI 2显示
    rssi_2_label = lv_label_create(right_container);
    lv_label_set_text(rssi_2_label, "RSSI2: -- dBm");
    set_telemetry_language_display(rssi_2_label);

    // LQ (Link Quality)显示
    lq_label = lv_label_create(right_container);
    lv_label_set_text(lq_label, "LQ: --%%");
    set_telemetry_language_display(lq_label);

    // SNR (Signal-to-Noise Ratio)显示
    snr_label = lv_label_create(right_container);
    lv_label_set_text(snr_label, "SNR: -- dB");
    set_telemetry_language_display(snr_label);

    // 天线选择显示
    antenna_label = lv_label_create(right_container);
    lv_label_set_text(antenna_label, "Ant: A");
    set_telemetry_language_display(antenna_label);

    // 模型匹配显示
    model_match_label = lv_label_create(right_container);
    lv_label_set_text(model_match_label, "Match: --");
    set_telemetry_language_display(model_match_label);

    // 姿态显示区域
    lv_obj_t* panel2 = lv_obj_create(content_container);
    lv_obj_set_width(panel2, lv_pct(100));
    lv_obj_set_height(panel2, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(panel2, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(panel2, 10, 0);
    lv_obj_set_style_pad_gap(panel2, 8, 0);

    // 在 panel2 中创建一个水平布局容器用于姿态角
    lv_obj_t* attitude_row = lv_obj_create(panel2);
    lv_obj_set_width(attitude_row, lv_pct(100));
    lv_obj_set_height(attitude_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(attitude_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(attitude_row, 0, 0);
    lv_obj_set_style_pad_gap(attitude_row, 10, 0);

    // Roll
    roll_label = lv_label_create(attitude_row);
    lv_label_set_text_fmt(roll_label, "%s --", text->roll_prefix);
    set_telemetry_language_display(roll_label);

    // Pitch
    pitch_label = lv_label_create(attitude_row);
    lv_label_set_text_fmt(pitch_label, "%s --", text->pitch_prefix);
    set_telemetry_language_display(pitch_label);

    // Yaw
    yaw_label = lv_label_create(attitude_row);
    lv_label_set_text_fmt(yaw_label, "%s --", text->yaw_prefix);
    set_telemetry_language_display(yaw_label);

    // 电压和电流显示
    lv_obj_t* battery_row = lv_obj_create(panel2);
    lv_obj_set_width(battery_row, lv_pct(100));
    lv_obj_set_height(battery_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(battery_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(battery_row, 0, 0);
    lv_obj_set_style_pad_gap(battery_row, 10, 0);

    // 电压显示
    voltage_label = lv_label_create(battery_row);
    lv_label_set_text(voltage_label, text->voltage_label);
    set_telemetry_language_display(voltage_label);

    // 电流显示
    current_label = lv_label_create(battery_row);
    lv_label_set_text(current_label, text->current_label);
    set_telemetry_language_display(current_label);

    // GPS状态显示
    gps_label = lv_label_create(panel2);
    lv_label_set_text(gps_label, text->gps_disconnected);
    set_telemetry_language_display(gps_label);

    // 高度显示
    altitude_label = lv_label_create(panel2);
    lv_label_set_text(altitude_label, text->altitude_label);
    set_telemetry_language_display(altitude_label);

    // 16个遥控通道显示区域
    lv_obj_t* channels_panel = lv_obj_create(content_container);
    lv_obj_set_width(channels_panel, lv_pct(100));
    lv_obj_set_height(channels_panel, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(channels_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(channels_panel, 10, 0);
    lv_obj_set_style_pad_gap(channels_panel, 5, 0);

    // 通道标题
    lv_obj_t* channels_title = lv_label_create(channels_panel);
    lv_label_set_text(channels_title, "Channels (CRSF)");
    set_telemetry_language_display(channels_title);

    // 创建通道显示行 (4个通道一行, 共4行)
    for (int row = 0; row < 4; row++) {
        lv_obj_t* channel_row = lv_obj_create(channels_panel);
        lv_obj_set_width(channel_row, lv_pct(100));
        lv_obj_set_height(channel_row, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(channel_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_all(channel_row, 0, 0);
        lv_obj_set_style_pad_gap(channel_row, 5, 0);

        for (int col = 0; col < 4; col++) {
            int ch_idx = row * 4 + col;
            channel_labels[ch_idx] = lv_label_create(channel_row);
            lv_label_set_text_fmt(channel_labels[ch_idx], "CH%d:--", ch_idx);
            lv_obj_set_width(channel_labels[ch_idx], lv_pct(25));
            set_telemetry_language_display(channel_labels[ch_idx]);
        }
    }

    // 扩展功能区域
    lv_obj_t* panel3 = lv_obj_create(content_container);
    lv_obj_set_width(panel3, lv_pct(100));
    lv_obj_set_height(panel3, LV_SIZE_CONTENT);
    lv_obj_t* title4 = lv_label_create(panel3);
    lv_label_set_text(title4, text->extended_functions);
    set_telemetry_language_display(title4);

    // 本地控制UI更新定时器已移除 (使用ELRS定义)
    // local_ui_update_timer = lv_timer_create(local_ui_update_task, 50, NULL);
}

/**
 * @brief 设置按钮事件处理，暂时没有用
 *
 * @param e 事件
 */
static void settings_btn_event_handler(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        // 处理设置按钮点击事件
        LV_LOG_USER("Settings button clicked");
    }
}

/**
 * @brief 摇杆事件处理
 *
 * @param e 事件
 */
static void slider_event_handler(lv_event_t* e) {
    lv_obj_t* slider = lv_event_get_target(e);
    int32_t value = lv_slider_get_value(slider);

    if (slider == throttle_slider) {
        LV_LOG_USER("Throttle slider value: %d", (int)value);
        // 发送控制命令到遥测服务
        if (telemetry_service_active) {
            int32_t direction_value = lv_slider_get_value(direction_slider);
            telemetry_service_send_control(value, direction_value);
        }
    } else if (slider == direction_slider) {
        LV_LOG_USER("Direction slider value: %d", (int)value);
        // 发送控制命令到遥测服务
        if (telemetry_service_active) {
            int32_t throttle_value = lv_slider_get_value(throttle_slider);
            telemetry_service_send_control(throttle_value, value);
        }
    }
}
/**
 * @brief 启动/停止按钮事件处理
 */
static void start_stop_btn_event_handler(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        const telemetry_text_t* text = get_current_telemetry_text();
        
        if (!telemetry_service_active) {
            // 启动遥测服务
            if (telemetry_service_start(telemetry_data_update_callback) == 0) {
                telemetry_service_active = true;

                // 更新按钮文本为"停止"
                lv_obj_t* btn_label = lv_obj_get_child(start_stop_btn, 0);
                if (btn_label) {
                    lv_label_set_text(btn_label, text->stop_button);
                }

                // 更改按钮颜色为红色
                lv_obj_set_style_bg_color(start_stop_btn, lv_color_hex(0xAA0000), 0);

                // 更新状态显示（如果存在）
                if (service_status_label) {
                    lv_label_set_text(service_status_label, text->status_running);
                }

                LV_LOG_USER("Telemetry service started");
            } else {
                LV_LOG_ERROR("Failed to start telemetry service");
                if (service_status_label) {
                    lv_label_set_text(service_status_label, text->status_start_failed);
                }
            }
        } else {
            // 停止遥测服务
            if (telemetry_service_active && telemetry_service_stop() == 0) {
                telemetry_service_active = false;

                // 更新按钮文本为"启动"
                lv_obj_t* btn_label = lv_obj_get_child(start_stop_btn, 0);
                if (btn_label) {
                    lv_label_set_text(btn_label, text->start_button);
                }

                // 更改按钮颜色为绿色
                lv_obj_set_style_bg_color(start_stop_btn, lv_color_hex(0x00AA00), 0);

                // 更新状态显示
                if (service_status_label) {
                    lv_label_set_text(service_status_label, text->status_stopped);
                }

                // 清空数据显示
                if (voltage_label) {
                    lv_label_set_text(voltage_label, text->voltage_label);
                }
                if (current_label) {
                    lv_label_set_text(current_label, text->current_label);
                }
                if (altitude_label) {
                    lv_label_set_text(altitude_label, text->altitude_label);
                }
                if (roll_label) {
                    lv_label_set_text_fmt(roll_label, "%s --", text->roll_prefix);
                }
                if (pitch_label) {
                    lv_label_set_text_fmt(pitch_label, "%s --", text->pitch_prefix);
                }
                if (yaw_label) {
                    lv_label_set_text_fmt(yaw_label, "%s --", text->yaw_prefix);
                }
                if (gps_label) {
                    lv_label_set_text(gps_label, text->gps_disconnected);
                }

                LV_LOG_USER("Telemetry service stopped");
            } else {
                // 停止失败或服务已经停止
                if (!telemetry_service_active) {
                    LV_LOG_WARN("Telemetry service already stopped");
                } else {
                    LV_LOG_ERROR("Failed to stop telemetry service");
                }
            }
        }
    }
}

static void telemetry_data_update_callback(const telemetry_data_t* data) {
    if (data == NULL)
        return;

    // 更新ELRS链路统计数据
    if (rssi_1_label && lv_obj_is_valid(rssi_1_label)) {
        int16_t rssi_1_dbm = telemetry_rssi_raw_to_dbm(data->uplink_rssi_1);
        lv_label_set_text_fmt(rssi_1_label, "RSSI1: %d dBm", rssi_1_dbm);
    }

    if (rssi_2_label && lv_obj_is_valid(rssi_2_label)) {
        int16_t rssi_2_dbm = telemetry_rssi_raw_to_dbm(data->uplink_rssi_2);
        lv_label_set_text_fmt(rssi_2_label, "RSSI2: %d dBm", rssi_2_dbm);
    }

    if (lq_label && lv_obj_is_valid(lq_label)) {
        lv_label_set_text_fmt(lq_label, "LQ: %d%%", data->link_quality);
    }

    if (snr_label && lv_obj_is_valid(snr_label)) {
        lv_label_set_text_fmt(snr_label, "SNR: %d dB", (int)data->snr);
    }

    if (antenna_label && lv_obj_is_valid(antenna_label)) {
        lv_label_set_text_fmt(antenna_label, "Ant: %c", data->antenna_select ? 'B' : 'A');
    }

    if (model_match_label && lv_obj_is_valid(model_match_label)) {
        lv_label_set_text(model_match_label, data->model_match ? "Match: OK" : "Match: --");
    }

    // 更新16个遥控通道
    if (data->channels_valid) {
        for (int i = 0; i < 16; i++) {
            if (channel_labels[i] && lv_obj_is_valid(channel_labels[i])) {
                uint16_t channel_val = data->channels[i];
                // CRSF值范围: 0-2047, 显示为百分比 (0-100%)
                uint8_t percent = (channel_val * 100) / 2047;
                lv_label_set_text_fmt(channel_labels[i], "CH%d:%d%%", i, percent);
            }
        }
    }

    // 更新扩展遥测数据 (IMU, 电池等)
    if (voltage_label && lv_obj_is_valid(voltage_label)) {
        lv_label_set_text_fmt(voltage_label, "V: %.2f V", data->voltage);
    }

    if (current_label && lv_obj_is_valid(current_label)) {
        lv_label_set_text_fmt(current_label, "I: %.2f A", data->current);
    }

    if (roll_label && lv_obj_is_valid(roll_label)) {
        lv_label_set_text_fmt(roll_label, "R: %.2f°", data->roll);
    }

    if (pitch_label && lv_obj_is_valid(pitch_label)) {
        lv_label_set_text_fmt(pitch_label, "P: %.2f°", data->pitch);
    }

    if (yaw_label && lv_obj_is_valid(yaw_label)) {
        lv_label_set_text_fmt(yaw_label, "Y: %.2f°", data->yaw);
    }

    if (altitude_label && lv_obj_is_valid(altitude_label)) {
        lv_label_set_text_fmt(altitude_label, "Alt: %.1f m", data->altitude);
    }

    // 更新GPS状态 (根据链路质量和信号强度推测)
    if (gps_label && lv_obj_is_valid(gps_label)) {
        if (data->link_quality > 80) {
            lv_label_set_text(gps_label, "GPS: OK");
        } else if (data->link_quality > 50) {
            lv_label_set_text(gps_label, "GPS: Search");
        } else {
            lv_label_set_text(gps_label, "GPS: Lost");
        }
    }
}

// 后续将添加更新遥测数据的函数
void ui_telemetry_update_data(float voltage, float current, float roll, float pitch, float yaw, float altitude) {
    const telemetry_text_t* text = get_current_telemetry_text();
    
    if (ui_get_current_language() == LANG_CHINESE) {
        lv_label_set_text_fmt(voltage_label, "电压: %.2f V", voltage);
        lv_label_set_text_fmt(current_label, "电流: %.2f A", current);
        lv_label_set_text_fmt(altitude_label, "高度: %.1f m", altitude);
    } else {
        lv_label_set_text_fmt(voltage_label, "Voltage: %.2f V", voltage);
        lv_label_set_text_fmt(current_label, "Current: %.2f A", current);
        lv_label_set_text_fmt(altitude_label, "Altitude: %.1f m", altitude);
    }
    
    // 更新姿态角 (前缀已经在text结构中定义)
    lv_label_set_text_fmt(roll_label, "%s %.2f", text->roll_prefix, roll);
    lv_label_set_text_fmt(pitch_label, "%s %.2f", text->pitch_prefix, pitch);
    lv_label_set_text_fmt(yaw_label, "%s %.2f", text->yaw_prefix, yaw);
}

/**
 * @brief 更新遥测界面的语言显示
 * 
 * 当语言设置改变时调用此函数来更新所有UI文本
 */
/*
void ui_telemetry_update_language(void) {
    const telemetry_text_t* text = get_current_telemetry_text();
    
    // 更新所有静态标签的文本
    if (voltage_label && lv_obj_is_valid(voltage_label)) {
        lv_label_set_text(voltage_label, text->voltage_label);
        set_telemetry_language_display(voltage_label);
    }
    
    if (current_label && lv_obj_is_valid(current_label)) {
        lv_label_set_text(current_label, text->current_label);
        set_telemetry_language_display(current_label);
    }
    
    if (altitude_label && lv_obj_is_valid(altitude_label)) {
        lv_label_set_text(altitude_label, text->altitude_label);
        set_telemetry_language_display(altitude_label);
    }
    
    if (gps_label && lv_obj_is_valid(gps_label)) {
        lv_label_set_text(gps_label, text->gps_disconnected);
        set_telemetry_language_display(gps_label);
    }
    
    if (roll_label && lv_obj_is_valid(roll_label)) {
        lv_label_set_text_fmt(roll_label, "%s --", text->roll_prefix);
        set_telemetry_language_display(roll_label);
    }
    
    if (pitch_label && lv_obj_is_valid(pitch_label)) {
        lv_label_set_text_fmt(pitch_label, "%s --", text->pitch_prefix);
        set_telemetry_language_display(pitch_label);
    }
    
    if (yaw_label && lv_obj_is_valid(yaw_label)) {
        lv_label_set_text_fmt(yaw_label, "%s --", text->yaw_prefix);
        set_telemetry_language_display(yaw_label);
    }
    
    // 更新启动/停止按钮文本
    if (start_stop_btn && lv_obj_is_valid(start_stop_btn)) {
        lv_obj_t* btn_label = lv_obj_get_child(start_stop_btn, 0);
        if (btn_label) {
            const char* btn_text = telemetry_service_active ? text->stop_button : text->start_button;
            lv_label_set_text(btn_label, btn_text);
            set_telemetry_language_display(btn_label);
        }
    }
    
    // 更新状态标签（如果存在）
    if (service_status_label && lv_obj_is_valid(service_status_label)) {
        const char* status_text = telemetry_service_active ? text->status_running : text->status_stopped;
        lv_label_set_text(service_status_label, status_text);
        set_telemetry_language_display(service_status_label);
    }
}
*/
// 添加UI清理函数
void ui_telemetry_cleanup(void) {
    // UI更新定时器已移除 (使用ELRS定义)
    // if (local_ui_update_timer) {
    //     lv_timer_del(local_ui_update_timer);
    //     local_ui_update_timer = NULL;
    // }

    // 停止遥测服务
    if (telemetry_service_active) {
        telemetry_service_stop();
        telemetry_service_active = false;
    }

    // 反初始化遥测服务
    telemetry_service_deinit();

    LV_LOG_USER("Telemetry UI cleanup completed");
}
