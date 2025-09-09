#ifndef LED_STATUS_MANAGER_H
#define LED_STATUS_MANAGER_H
#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "ws2812.h"

#ifdef __cplusplus
extern "C" {
#endif

// LED状态样式枚举
typedef enum {
    LED_STYLE_OFF = 0,          // 关闭
    LED_STYLE_GREEN_BREATHING,  // 绿色呼吸
    LED_STYLE_BLUE_RED_BLINK,   // 蓝红闪烁0.5秒
    LED_STYLE_RED_SOLID,        // 红色常亮
    LED_STYLE_GREEN_FAST_BLINK, // 绿色快闪0.2秒
    LED_STYLE_CUSTOM,           // 自定义样式
    LED_STYLE_MAX
} led_style_t;

// LED状态优先级
typedef enum {
    LED_PRIORITY_LOW = 0, // 低优先级
    LED_PRIORITY_NORMAL,  // 普通优先级
    LED_PRIORITY_HIGH,    // 高优先级
    LED_PRIORITY_CRITICAL // 紧急优先级
} led_priority_t;

// 自定义LED样式配置
typedef struct {
    ws2812_color_t color1; // 主颜色
    ws2812_color_t color2; // 辅助颜色（用于闪烁等效果）
    uint32_t period_ms;    // 周期时间（毫秒）
    uint32_t on_time_ms;   // 亮起时间（毫秒，用于闪烁）
    uint8_t brightness;    // 亮度 (0-255)
    bool fade_enabled;     // 是否启用渐变效果
} led_custom_config_t;

// LED状态请求结构
typedef struct {
    led_style_t style;          // LED样式
    led_priority_t priority;    // 优先级
    uint32_t duration_ms;       // 持续时间（0表示永久）
    led_custom_config_t custom; // 自定义配置（仅当style为LED_STYLE_CUSTOM时使用）
} led_status_request_t;

// LED管理器配置
typedef struct {
    uint16_t led_count;       // LED数量
    uint8_t task_priority;    // 任务优先级
    uint32_t task_stack_size; // 任务栈大小
    uint8_t queue_size;       // 请求队列大小
} led_manager_config_t;

// 默认配置
#define LED_MANAGER_DEFAULT_CONFIG()                                                               \
    {.led_count = 1, .task_priority = 2, .task_stack_size = 2048, .queue_size = 8}

/**
 * @brief 初始化LED状态管理器
 * @param config 管理器配置
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t led_status_manager_init(const led_manager_config_t* config);

/**
 * @brief 反初始化LED状态管理器
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t led_status_manager_deinit(void);

/**
 * @brief 设置LED状态
 * @param request LED状态请求
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t led_status_set(const led_status_request_t* request);

/**
 * @brief 设置预定义LED样式
 * @param style LED样式
 * @param priority 优先级
 * @param duration_ms 持续时间（0表示永久）
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t led_status_set_style(led_style_t style, led_priority_t priority, uint32_t duration_ms);

/**
 * @brief 设置自定义LED样式
 * @param custom 自定义配置
 * @param priority 优先级
 * @param duration_ms 持续时间（0表示永久）
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t led_status_set_custom(const led_custom_config_t* custom, led_priority_t priority,
                                uint32_t duration_ms);

/**
 * @brief 清除LED状态（关闭LED）
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t led_status_clear(void);

/**
 * @brief 获取当前LED样式
 * @return 当前LED样式
 */
led_style_t led_status_get_current_style(void);

/**
 * @brief 检查LED管理器是否已初始化
 * @return true 已初始化，false 未初始化
 */
bool led_status_manager_is_initialized(void);

#ifdef __cplusplus
}
#endif
#endif
