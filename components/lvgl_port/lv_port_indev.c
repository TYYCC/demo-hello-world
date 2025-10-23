/**
 * @file lv_port_indev.c
 * @brief LVGL Input Device Port for ESP32-S3 with XPT2046
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_indev.h"
#include "esp_log.h"

#if USE_FT6336G_TOUCH
#include "ft6336g.h" // 使用 FT6336G 驱动
#endif

#include "gt911.h"
#include "esp_err.h"

/*********************
 *      DEFINES
 *********************/

static const char* TAG = "lv_port_indev";

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void touchpad_init(void);
static void touchpad_read(lv_indev_drv_t* indev_drv, lv_indev_data_t* data);
#if !USE_FT6336G_TOUCH
static void touchpad_get_xy(lv_coord_t* x, lv_coord_t* y);
#endif

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_indev_t* indev_touchpad;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_port_indev_init(void) {
    /**
     * Here you will find example implementation of input devices supported by LittelvGL:
     *  - Touchpad
     *  - Mouse (with cursor support)
     *  - Keypad (supports GUI usage only with key)
     *  - Encoder (supports GUI usage only with: left, right, push)
     *  - Button (external buttons to press points on the screen)
     *
     *  The `..._read()` function are only examples.
     *  You should shape them according to your hardware
     */

    /*------------------
     * Touchpad
     * -----------------*/

    /*Initialize your touchpad if you have*/
    touchpad_init();

    /*Register a touchpad input device*/
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    indev_touchpad = lv_indev_drv_register(&indev_drv);

    /* input device ready */
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/*------------------
 * Touchpad
 * -----------------*/

/*Initialize your touchpad*/
static void touchpad_init(void) {
    // GT911 驱动已在 components_init 中初始化
}

/*Will be called by the library to read the touchpad*/
static void touchpad_read(lv_indev_drv_t* indev_drv, lv_indev_data_t* data) {
    static lv_coord_t last_x = 0;
    static lv_coord_t last_y = 0;

    uint8_t num_points;
    ft6336g_touch_point_t point;

    esp_err_t ret = ft6336g_read_touch_points(&point, &num_points);
    if (ret == ESP_OK && num_points > 0) {
        last_x = point.x;
        last_y = point.y;
        data->state = LV_INDEV_STATE_PR;
        ESP_LOGD(TAG, "Touchpad read: x=%d, y=%d", last_x, last_y);
    } else {
        data->state = LV_INDEV_STATE_REL;
    }

    data->point.x = last_x;
    data->point.y = last_y;
}
