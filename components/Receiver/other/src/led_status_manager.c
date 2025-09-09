#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "led_status_manager.h"

static const char* TAG = "led_status_mgr";

// 管理器状态
static bool s_initialized = false;
static TaskHandle_t s_led_task_handle = NULL;
static QueueHandle_t s_request_queue = NULL;
static TimerHandle_t s_duration_timer = NULL;

// 使用动态分配的内存
static led_status_request_t* s_current_request = NULL;
static led_style_t s_current_style = LED_STYLE_OFF;
static uint32_t s_effect_counter = 0;
static uint32_t s_last_update_time = 0;

// 配置 - 使用动态分配的内存
static led_manager_config_t* s_config = NULL;

// 内部函数声明
static void led_manager_task(void* pvParameters);
static void duration_timer_callback(TimerHandle_t xTimer);
static esp_err_t apply_led_style(const led_status_request_t* request);
static esp_err_t update_led_effect(void);
static esp_err_t render_green_breathing(void);
static esp_err_t render_blue_red_blink(void);
static esp_err_t render_red_solid(void);
static esp_err_t render_green_fast_blink(void);
static esp_err_t render_custom_style(const led_custom_config_t* config);

/**
 * @brief 初始化LED状态管理器
 *
 * @param config 配置参数
 * @return esp_err_t
 */
esp_err_t led_status_manager_init(const led_manager_config_t* config) {
    if (s_initialized) {
        ESP_LOGW(TAG, "LED状态管理器已初始化");
        return ESP_OK;
    }

    if (!config) {
        ESP_LOGE(TAG, "配置参数为空");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "初始化LED状态管理器...");

    // PSRAM动态分配配置内存
    s_config = heap_caps_malloc(sizeof(led_manager_config_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    memcpy(s_config, config, sizeof(led_manager_config_t));

    // PSRAM动态分配当前内存
    s_current_request = heap_caps_malloc(sizeof(led_status_request_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    memset(s_current_request, 0, sizeof(led_status_request_t));

    // 初始化WS2812
    esp_err_t ret = ws2812_init(s_config->led_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WS2812初始化失败: %s", esp_err_to_name(ret));
        free(s_current_request);
        free(s_config);
        s_current_request = NULL;
        s_config = NULL;
        return ret;
    }

    // 创建请求队列
    s_request_queue = xQueueCreate(s_config->queue_size, sizeof(led_status_request_t));
    if (!s_request_queue) {
        ESP_LOGE(TAG, "创建请求队列失败");
        ws2812_deinit();
        free(s_current_request);
        free(s_config);
        s_current_request = NULL;
        s_config = NULL;
        return ESP_ERR_NO_MEM;
    }

    // 创建持续时间定时器（初始周期为1秒，后续会改变）
    s_duration_timer = xTimerCreate("led_duration", pdMS_TO_TICKS(1000), pdFALSE, (void*)0,
                                    duration_timer_callback);
    if (!s_duration_timer) {
        ESP_LOGE(TAG, "创建持续时间定时器失败");
        vQueueDelete(s_request_queue);
        s_request_queue = NULL;
        ws2812_deinit();
        free(s_current_request);
        free(s_config);
        s_current_request = NULL;
        s_config = NULL;
        return ESP_ERR_NO_MEM;
    }

    // 初始化状态
    s_current_style = LED_STYLE_OFF;
    s_effect_counter = 0;
    s_last_update_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    // 清空LED
    ws2812_clear_all();
    ws2812_refresh();

    // 设置初始化标志为true，确保任务启动时状态正确
    s_initialized = true;

    // 创建LED管理任务
    ESP_LOGI(TAG, "创建LED管理任务，栈大小: %d 字节", s_config->task_stack_size);
    BaseType_t task_ret = xTaskCreate(led_manager_task, "led_manager", s_config->task_stack_size,
                                      NULL, s_config->task_priority, &s_led_task_handle);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "创建LED管理任务失败");
        s_initialized = false;
        xTimerDelete(s_duration_timer, 0);
        s_duration_timer = NULL;
        vQueueDelete(s_request_queue);
        s_request_queue = NULL;
        ws2812_deinit();
        free(s_current_request);
        free(s_config);
        s_current_request = NULL;
        s_config = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "LED状态管理器初始化成功");
    return ESP_OK;
}

/**
 * @brief 反初始化LED状态管理器
 *
 * @return esp_err_t
 */
esp_err_t led_status_manager_deinit(void) {
    if (!s_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "反初始化LED状态管理器...");

    // 首先设置退出标志，让任务能够退出
    s_initialized = false;

    // 等待一小段时间让任务检测到退出标志
    vTaskDelay(pdMS_TO_TICKS(20));

    // 停止定时器
    if (s_duration_timer) {
        xTimerStop(s_duration_timer, portMAX_DELAY);
        xTimerDelete(s_duration_timer, portMAX_DELAY);
        s_duration_timer = NULL;
    }

    // 删除任务
    if (s_led_task_handle) {
        vTaskDelete(s_led_task_handle);
        s_led_task_handle = NULL;
    }

    // 删除队列
    if (s_request_queue) {
        vQueueDelete(s_request_queue);
        s_request_queue = NULL;
    }

    // 清空LED并反初始化
    ws2812_clear_all();
    ws2812_refresh();
    ws2812_deinit();

    // 释放动态分配的内存
    if (s_current_request) {
        free(s_current_request);
        s_current_request = NULL;
    }
    if (s_config) {
        free(s_config);
        s_config = NULL;
    }

    ESP_LOGI(TAG, "LED状态管理器反初始化完成");
    return ESP_OK;
}

/**
 * @brief 设置LED状态
 *
 * @param request LED状态请求
 * @return esp_err_t
 */
esp_err_t led_status_set(const led_status_request_t* request) {
    if (!s_initialized) {
        ESP_LOGE(TAG, "LED状态管理器未初始化");
        return ESP_ERR_INVALID_STATE;
    }

    if (!request) {
        ESP_LOGE(TAG, "请求参数为空");
        return ESP_ERR_INVALID_ARG;
    }

    if (request->style >= LED_STYLE_MAX) {
        ESP_LOGE(TAG, "无效的LED样式: %d", request->style);
        return ESP_ERR_INVALID_ARG;
    }

    // 发送请求到队列
    if (xQueueSend(s_request_queue, request, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "请求队列已满，丢弃请求");
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

/**
 * @brief 设置LED样式
 * @param style LED样式
 * @param priority 优先级
 * @param duration_ms 持续时间（毫秒）
 * @return esp_err_t 成功返回ESP_OK
 * @note 持续时间为0表示无限持续
 */
esp_err_t led_status_set_style(led_style_t style, led_priority_t priority, uint32_t duration_ms) {
    led_status_request_t request = {
        .style = style, .priority = priority, .duration_ms = duration_ms, .custom = {}};
    return led_status_set(&request);
}

/**
 * @brief 设置自定义LED样式
 *
 * @param custom 自定义配置
 * @param priority 优先级
 * @param duration_ms 持续时间（毫秒）
 * @return esp_err_t
 */
esp_err_t led_status_set_custom(const led_custom_config_t* custom, led_priority_t priority,
                                uint32_t duration_ms) {
    if (!custom) {
        return ESP_ERR_INVALID_ARG;
    }

    led_status_request_t request = {.style = LED_STYLE_CUSTOM,
                                    .priority = priority,
                                    .duration_ms = duration_ms,
                                    .custom = *custom};
    return led_status_set(&request);
}

// 清除LED状态
esp_err_t led_status_clear(void) {
    return led_status_set_style(LED_STYLE_OFF, LED_PRIORITY_NORMAL, 0);
}

// 获取当前LED样式
led_style_t led_status_get_current_style(void) { return s_current_style; }

// 检查LED管理器是否已初始化
bool led_status_manager_is_initialized(void) { return s_initialized; }

// LED管理任务
static void led_manager_task(void* pvParameters) {
    ESP_LOGI(TAG, "LED管理任务启动");

    // 使用栈上的小结构体，减少内存使用
    led_status_request_t request;
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t update_period = pdMS_TO_TICKS(20); // 50Hz更新频率

    // 等待初始化完成
    vTaskDelay(pdMS_TO_TICKS(10));

    while (s_initialized) {
        if (xQueueReceive(s_request_queue, &request, 0) == pdTRUE) {
            if (s_current_request == NULL || 
                request.priority >= s_current_request->priority ||
                s_current_style == LED_STYLE_OFF) {
                ESP_LOGI(TAG, "应用新的LED样式: %d, 优先级: %d", request.style, request.priority);

                // 停止当前定时器
                if (s_duration_timer != NULL && xTimerIsTimerActive(s_duration_timer) == pdTRUE) {
                    xTimerStop(s_duration_timer, portMAX_DELAY);
                }

                if (s_current_request != NULL) {
                    memcpy(s_current_request, &request, sizeof(led_status_request_t));
                }
                apply_led_style(&request);

                if (request.duration_ms > 0 && s_duration_timer != NULL) {
                    xTimerChangePeriod(s_duration_timer, pdMS_TO_TICKS(request.duration_ms),
                                       portMAX_DELAY);
                    xTimerStart(s_duration_timer, portMAX_DELAY);
                }
            } else {
                ESP_LOGD(TAG, "忽略低优先级请求: %d < %d", request.priority,
                         s_current_request ? s_current_request->priority : 0);
            }
        }

        // 更新LED效果
        update_led_effect();

        // 等待下次更新
        vTaskDelayUntil(&last_wake_time, update_period);
    }

    ESP_LOGI(TAG, "LED管理任务退出");
}

// 持续时间定时器回调
static void duration_timer_callback(TimerHandle_t xTimer) {
    ESP_LOGI(TAG, "LED样式持续时间到期，切换到关闭状态");

    static const led_status_request_t clear_request = {
        .style = LED_STYLE_OFF, .priority = LED_PRIORITY_NORMAL, .duration_ms = 0, .custom = {}};

    if (s_request_queue != NULL) {
        xQueueSend(s_request_queue, &clear_request, 0);
    }
}

// 应用LED样式
static esp_err_t apply_led_style(const led_status_request_t* request) {
    s_current_style = request->style;
    s_effect_counter = 0;
    s_last_update_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    ESP_LOGD(TAG, "应用LED样式: %d", s_current_style);

    // 对于静态样式，立即渲染
    if (s_current_style == LED_STYLE_RED_SOLID) {
        return render_red_solid();
    } else if (s_current_style == LED_STYLE_OFF) {
        ws2812_clear_all();
        return ws2812_refresh();
    }

    return ESP_OK;
}

/**
 * @brief 更新LED效果
 * @return ESP_OK 成功，其他值表示错误
 */
static esp_err_t update_led_effect(void) {
    switch (s_current_style) {
    case LED_STYLE_OFF:
        // 无需更新
        break;

    case LED_STYLE_GREEN_BREATHING:
        return render_green_breathing();

    case LED_STYLE_BLUE_RED_BLINK:
        return render_blue_red_blink();

    case LED_STYLE_RED_SOLID:
        // 静态样式，无需更新
        break;

    case LED_STYLE_GREEN_FAST_BLINK:
        return render_green_fast_blink();

    case LED_STYLE_CUSTOM:
        return render_custom_style(&s_current_request->custom);

    default:
        ESP_LOGW(TAG, "未知的LED样式: %d", s_current_style);
        break;
    }

    return ESP_OK;
}

/**
 * @brief 渲染绿色呼吸效果（2秒周期）
 * @return ESP_OK 成功，其他值表示错误
 */
static esp_err_t render_green_breathing(void) {
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    float phase = (current_time % 2000) / 2000.0f; // 2秒周期
    float brightness = (sinf(2.0f * M_PI * phase) + 1.0f) / 2.0f;

    ws2812_color_t green = ws2812_rgb(0, (uint8_t)(255 * brightness), 0);
    ws2812_set_all(green);
    return ws2812_refresh();
}

/**
 * @brief 渲染蓝红闪烁效果（0.5秒周期）
 * @return ESP_OK 成功，其他值表示错误
 */
static esp_err_t render_blue_red_blink(void) {
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    bool is_blue_phase = ((current_time % 1000) < 500); // 1秒周期，前0.5秒蓝色，后0.5秒红色

    ws2812_color_t color = is_blue_phase ? ws2812_rgb(0, 0, 255) : ws2812_rgb(255, 0, 0);
    ws2812_set_all(color);
    return ws2812_refresh();
}

/**
 * @brief 渲染红色常亮
 * @return ESP_OK 成功，其他值表示错误
 */
static esp_err_t render_red_solid(void) {
    ws2812_color_t red = ws2812_rgb(255, 0, 0);
    ws2812_set_all(red);
    return ws2812_refresh();
}

// 渲染绿色快闪效果（0.2秒周期）
static esp_err_t render_green_fast_blink(void) {
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    bool is_on = ((current_time % 400) < 200); // 0.4秒周期，前0.2秒亮，后0.2秒灭

    ws2812_color_t color = is_on ? ws2812_rgb(0, 255, 0) : ws2812_rgb(0, 0, 0);
    ws2812_set_all(color);
    return ws2812_refresh();
}

/**
 * @brief 渲染自定义样式
 * @param config 自定义配置
 * @return ESP_OK 成功，其他值表示错误
 */
static esp_err_t render_custom_style(const led_custom_config_t* config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    if (config->period_ms == 0) {
        // 静态颜色
        ws2812_color_t color = ws2812_scale_color(config->color1, config->brightness);
        ws2812_set_all(color);
    } else {
        float phase = (current_time % config->period_ms) / (float)config->period_ms;

        if (config->fade_enabled) {
            // 渐变效果
            float brightness_factor = (sinf(2.0f * M_PI * phase) + 1.0f) / 2.0f;
            ws2812_color_t color = ws2812_scale_color(
                config->color1, (uint8_t)(config->brightness * brightness_factor));
            ws2812_set_all(color);
        } else {
            // 闪烁效果
            bool is_on = (current_time % config->period_ms) < config->on_time_ms;
            ws2812_color_t color = is_on ? ws2812_scale_color(config->color1, config->brightness)
                                         : ws2812_scale_color(config->color2, config->brightness);
            ws2812_set_all(color);
        }
    }

    return ws2812_refresh();
}