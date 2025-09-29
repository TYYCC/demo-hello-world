#include "key.h"
#include "esp_log.h"
#include "joystick_adc.h"
#include <esp_timer.h>

#define KEY_THRESHOLD 50        // 归一化值阈值，超过此值判定为按下
#define KEY_REPEAT_DELAY_MS 500 // 保持按下后多少毫秒后再次输出事件

// Add defines
#define LONG_PRESS_DELAY_MS 1000 // 长按阈值

// Extend static variables for all keys
static bool up_active = false;
static bool up_long_active = false;
static uint64_t up_ts = 0;
static uint64_t up_last_repeat_ts = 0;

static bool down_active = false;
static bool down_long_active = false;
static uint64_t down_ts = 0;
static uint64_t down_last_repeat_ts = 0;

static bool left_active = false;
static bool left_long_active = false;
static uint64_t left_ts = 0;
static uint64_t left_last_repeat_ts = 0;

static bool right_active = false;
static bool right_long_active = false;
static uint64_t right_ts = 0;
static uint64_t right_last_repeat_ts = 0;

static bool center_active = false;
static bool center_long_active = false;
static uint64_t center_ts = 0;
static uint64_t center_last_repeat_ts = 0;

static bool vol_up_active = false;
static bool vol_up_long_active = false;
static uint64_t vol_up_ts = 0;
static uint64_t vol_up_last_repeat_ts = 0;

static bool vol_down_active = false;
static bool vol_down_long_active = false;
static uint64_t vol_down_ts = 0;
static uint64_t vol_down_last_repeat_ts = 0;

// Add variable for ADC key enable, default false
static bool enable_adc_key = false;

void key_init(void) {
    if (joystick_adc_init() != ESP_OK) {
        ESP_LOGW("KEY", "Failed to init joystick ADC");
    }

    // Initialize GPIOs for 74HC165
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << KEY_SH_LD_Pin) | (1ULL << KEY_SCK_Pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    io_conf.pin_bit_mask = (1ULL << KEY_INPUT_Pin);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE; // Assuming pull-up if needed
    gpio_config(&io_conf);

    KEY_SH_LD_H;
    KEY_SCK_L;
}

// Add function implementation
void set_adc_key_enable(bool enable) { enable_adc_key = enable; }

key_dir_t key_scan(void) {
    joystick_data_t data;
    bool adc_read_ok = (joystick_adc_read(&data) == ESP_OK);

    // Read 74HC165
    static bool key_static[16];
    KEY_SH_LD_L; // Load parallel data
    KEY_SH_LD_H; // Enable serial output
    key_static[0] = KEY_INPUT;
    for (uint8_t i = 1; i < 16; i++) {
        KEY_SCK_H;
        key_static[i] = KEY_INPUT;
        KEY_SCK_L;
    }

// Assume active low, change indices as per hardware
#define UP_INDEX 0
#define DOWN_INDEX 1
#define LEFT_INDEX 2
#define RIGHT_INDEX 3
#define CENTER_INDEX 4
#define VOL_UP_INDEX 5
#define VOL_DOWN_INDEX 6

    bool key_up = !key_static[UP_INDEX];
    bool key_down = !key_static[DOWN_INDEX];
    bool key_left = !key_static[LEFT_INDEX];
    bool key_right = !key_static[RIGHT_INDEX];
    bool key_center = !key_static[CENTER_INDEX];
    bool key_vol_up = !key_static[VOL_UP_INDEX];
    bool key_vol_down = !key_static[VOL_DOWN_INDEX];

    // ADC directions (assuming no center for ADC)
    bool adc_up = enable_adc_key && adc_read_ok && (data.norm_joy1_y > KEY_THRESHOLD);
    bool adc_down = enable_adc_key && adc_read_ok && (data.norm_joy1_y < -KEY_THRESHOLD);
    bool adc_left = enable_adc_key && adc_read_ok && (data.norm_joy1_x < -KEY_THRESHOLD);
    bool adc_right = enable_adc_key && adc_read_ok && (data.norm_joy1_x > KEY_THRESHOLD);

    // Fused pressed states
    bool up_pressed = adc_up || key_up;
    bool down_pressed = adc_down || key_down;
    bool left_pressed = adc_left || key_left;
    bool right_pressed = adc_right || key_right;
    bool center_pressed = key_center;     // No ADC fusion
    bool vol_up_pressed = key_vol_up;     // No ADC fusion
    bool vol_down_pressed = key_vol_down; // No ADC fusion

    key_dir_t events = KEY_NONE;
    uint64_t now = esp_timer_get_time() / 1000ULL;

// Macro to handle logic for each key
#define HANDLE_KEY(dir, pressed_var, active_var, long_active_var, ts_var, repeat_ts_var, KEY_DIR,  \
                   KEY_DIR_LONG)                                                                   \
    if (pressed_var) {                                                                             \
        if (!active_var) {                                                                         \
            events |= KEY_DIR;                                                                     \
            active_var = true;                                                                     \
            ts_var = now;                                                                          \
            repeat_ts_var = now;                                                                   \
        } else {                                                                                   \
            if (!long_active_var && (now - ts_var >= LONG_PRESS_DELAY_MS)) {                       \
                events |= KEY_DIR_LONG;                                                            \
                long_active_var = true;                                                            \
                repeat_ts_var = now;                                                               \
            } else if (now - repeat_ts_var >= KEY_REPEAT_DELAY_MS) {                               \
                events |= KEY_DIR;                                                                 \
                repeat_ts_var = now;                                                               \
            }                                                                                      \
        }                                                                                          \
    } else {                                                                                       \
        active_var = false;                                                                        \
        long_active_var = false;                                                                   \
    }

    HANDLE_KEY(up, up_pressed, up_active, up_long_active, up_ts, up_last_repeat_ts, KEY_UP,
               KEY_UP_LONG)
    HANDLE_KEY(down, down_pressed, down_active, down_long_active, down_ts, down_last_repeat_ts,
               KEY_DOWN, KEY_DOWN_LONG)
    HANDLE_KEY(left, left_pressed, left_active, left_long_active, left_ts, left_last_repeat_ts,
               KEY_LEFT, KEY_LEFT_LONG)
    HANDLE_KEY(right, right_pressed, right_active, right_long_active, right_ts,
               right_last_repeat_ts, KEY_RIGHT, KEY_RIGHT_LONG)
    HANDLE_KEY(center, center_pressed, center_active, center_long_active, center_ts,
               center_last_repeat_ts, KEY_CENTER, KEY_CENTER_LONG)
    HANDLE_KEY(vol_up, vol_up_pressed, vol_up_active, vol_up_long_active, vol_up_ts,
               vol_up_last_repeat_ts, KEY_VOL_UP, KEY_VOL_UP_LONG)
    HANDLE_KEY(vol_down, vol_down_pressed, vol_down_active, vol_down_long_active, vol_down_ts,
               vol_down_last_repeat_ts, KEY_VOL_DOWN, KEY_VOL_DOWN_LONG)

#undef HANDLE_KEY

    if (!adc_read_ok) {
        // If ADC read failed, reset ADC-related states if needed, but since fused, already handled
    }

    return events;
}

// Remove or comment out the separate key_scanner function if not needed
// key_dir_t key_scanner(void) { ... } // Can remove this