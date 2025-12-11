/**
 * @file lv_port_indev.c
 * @brief LVGL Input Device Port for ESP32-S3 with GT911 (Interrupt-driven)
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_indev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_attr.h"

#if USE_FT6336G_TOUCH
#include "ft6336g.h" // 使用 FT6336G 驱动
#endif

#include "gt911.h"
#include "esp_err.h"

/*********************
 *      DEFINES
 *********************/

static const char* TAG = "lv_port_indev";

// 中断驱动模式：使用原子操作保存最新触摸坐标
#define TOUCH_QUEUE_SIZE 1  // 只需要最新的一个坐标

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
static volatile uint16_t touch_x = 0;           // 最新的 X 坐标
static volatile uint16_t touch_y = 0;           // 最新的 Y 坐标
static volatile bool touch_has_data = false;    // 有新数据标志
static SemaphoreHandle_t touch_semaphore = NULL;  // 信号量，用于中断直接唤醒任务
static volatile bool touch_pressed = false;

/**********************
 *      MACROS
 **********************/

// 中断处理任务优先级和大小
#define TOUCH_TASK_PRIORITY 6  // 提高优先级以获得快速响应
#define TOUCH_TASK_STACK_SIZE 2048

// 中断回调函数 - 由 GT911 中断处理程序触发
static void gt911_irq_callback(void);

// 触摸处理任务 - 处理中断触发的触摸数据读取
static void touch_handler_task(void* arg);

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
    // 创建二值信号量 - 用于中断直接唤醒任务
    touch_semaphore = xSemaphoreCreateBinary();
    if (touch_semaphore == NULL) {
        ESP_LOGE(TAG, "Failed to create touch semaphore");
        return;
    }
    
    // GT911 驱动已在 components_init 中初始化
    
    // 注册 GT911 中断回调函数
    gt911_register_irq_callback(gt911_irq_callback);
    
    // 创建触摸处理任务 (高优先级，快速响应中断)
    xTaskCreate(touch_handler_task, "touch_handler", TOUCH_TASK_STACK_SIZE, NULL, 
                TOUCH_TASK_PRIORITY, NULL);
    
    ESP_LOGI(TAG, "Touchpad initialized in interrupt-driven mode (low latency)");
}

/*Will be called by the library to read the touchpad*/
static void touchpad_read(lv_indev_drv_t* indev_drv, lv_indev_data_t* data) {
    static lv_coord_t last_x = 0;
    static lv_coord_t last_y = 0;

    // 检查是否有新的触摸数据
    if (touch_has_data) {
        last_x = touch_x;
        last_y = touch_y;
        touch_has_data = false;
        touch_pressed = true;
        data->state = LV_INDEV_STATE_PR;
    } else if (touch_pressed) {
        // 检查是否还有触摸点
        uint8_t num_points = gt911_get_touch_points();
        if (num_points > 0) {
            // 仍有触摸，保持 pressed 状态
            data->state = LV_INDEV_STATE_PR;
        } else {
            // 触摸释放
            touch_pressed = false;
            data->state = LV_INDEV_STATE_REL;
        }
    } else {
        // 无触摸
        data->state = LV_INDEV_STATE_REL;
    }

    data->point.x = last_x;
    data->point.y = last_y;
}

// GT911 中断回调函数 - 在中断上下文中被调用 (IRAM 安全)
static IRAM_ATTR void gt911_irq_callback(void) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(touch_semaphore, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

// 触摸处理任务 - 由中断直接唤醒，无轮询延迟
static void touch_handler_task(void* arg) {
    uint8_t num_points;
    gt911_touch_point_t point;
    
    while (1) {
        if (xSemaphoreTake(touch_semaphore, portMAX_DELAY) == pdTRUE) {
            // 清除中断标志
            gt911_clear_irq_flag();
            
            // 读取触摸数据
            esp_err_t ret = gt911_read_touch_points(&point, &num_points);
            
            if (ret == ESP_OK && num_points > 0) {
                // 直接更新全局变量（原子操作）
                touch_x = point.x;
                touch_y = point.y;
                touch_has_data = true;  // 标记有新数据，供 touchpad_read 读取
            }
            
            // 极短的防抖延迟
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}
