#ifndef PWM_CONTROLLER_H
#define PWM_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/ledc.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// PWM配置常量
#define PWM_CHANNEL_COUNT           8       // 支持的PWM通道数量
#define PWM_DEFAULT_FREQUENCY       10000   // 默认频率10kHz
#define PWM_MAX_DUTY_PERCENT        96      // 最大占空比96%
#define PWM_RESOLUTION_BITS         10      // PWM分辨率10位(1024级)
#define PWM_MAX_DUTY_VALUE          ((1 << PWM_RESOLUTION_BITS) - 1) // 最大占空比值

// GPIO引脚定义
#define PWM_GPIO_PIN_1              3
#define PWM_GPIO_PIN_2              4
#define PWM_GPIO_PIN_3              5
#define PWM_GPIO_PIN_4              6
#define PWM_GPIO_PIN_5              7
#define PWM_GPIO_PIN_6              8
#define PWM_GPIO_PIN_7              9
#define PWM_GPIO_PIN_8              10

// PWM通道配置结构体
typedef struct {
    uint8_t channel;        // LEDC通道号
    uint8_t gpio_pin;       // GPIO引脚号
    uint32_t frequency;     // PWM频率
    uint32_t duty_value;    // 占空比值(0-1023)
    float duty_percent;     // 占空比百分比(0-100%)
    bool enabled;           // 通道是否启用
} pwm_channel_config_t;

// PWM控制器配置结构体
typedef struct {
    uint32_t frequency;                         // 全局频率设置
    ledc_timer_t timer_num;                     // LEDC定时器号
    ledc_mode_t speed_mode;                     // LEDC速度模式
    pwm_channel_config_t channels[PWM_CHANNEL_COUNT]; // 通道配置数组
    bool initialized;                           // 初始化状态
} pwm_controller_config_t;

// PWM统计信息结构体
typedef struct {
    uint32_t total_updates;     // 总更新次数
    uint32_t frequency_changes; // 频率变更次数
    uint64_t last_update_time;  // 最后更新时间戳
} pwm_controller_stats_t;

/**
 * @brief 初始化PWM控制器
 * 
 * @param frequency 初始频率(Hz)
 * @return esp_err_t ESP_OK表示成功
 */
esp_err_t pwm_controller_init(uint32_t frequency);

/**
 * @brief 反初始化PWM控制器
 * 
 * @return esp_err_t ESP_OK表示成功
 */
esp_err_t pwm_controller_deinit(void);

/**
 * @brief 设置PWM频率
 * 
 * @param frequency 新频率(Hz)
 * @return esp_err_t ESP_OK表示成功
 */
esp_err_t pwm_controller_set_frequency(uint32_t frequency);

/**
 * @brief 设置单个通道的占空比
 * 
 * @param channel 通道号(0-7)
 * @param duty_percent 占空比百分比(0-96%)
 * @return esp_err_t ESP_OK表示成功
 */
esp_err_t pwm_controller_set_duty(uint8_t channel, float duty_percent);

/**
 * @brief 批量设置多个通道的占空比
 * 
 * @param channel_values 通道值数组(0-1000对应0-100%)
 * @param channel_count 通道数量
 * @return esp_err_t ESP_OK表示成功
 */
esp_err_t pwm_controller_set_channels(const uint16_t *channel_values, uint8_t channel_count);

/**
 * @brief 启用或禁用PWM通道
 * 
 * @param channel 通道号(0-7)
 * @param enabled true启用，false禁用
 * @return esp_err_t ESP_OK表示成功
 */
esp_err_t pwm_controller_enable_channel(uint8_t channel, bool enabled);

/**
 * @brief 停止所有PWM输出
 * 
 * @return esp_err_t ESP_OK表示成功
 */
esp_err_t pwm_controller_stop_all(void);

/**
 * @brief 获取PWM控制器配置
 * 
 * @return const pwm_controller_config_t* 配置指针
 */
const pwm_controller_config_t* pwm_controller_get_config(void);

/**
 * @brief 获取PWM统计信息
 * 
 * @return const pwm_controller_stats_t* 统计信息指针
 */
const pwm_controller_stats_t* pwm_controller_get_stats(void);

/**
 * @brief 打印PWM状态信息
 */
void pwm_controller_print_status(void);

/**
 * @brief 重置PWM统计信息
 */
void pwm_controller_reset_stats(void);

/**
 * @brief 根据频率自动计算最佳PWM分辨率
 * @param frequency 目标频率(Hz)
 * @return 推荐的分辨率位数(1-20位)
 */
uint8_t pwm_controller_calculate_optimal_resolution(uint32_t frequency);

/**
 * @brief 验证频率和分辨率组合是否可行
 * @param frequency 目标频率(Hz)
 * @param resolution_bits 分辨率位数
 * @return true表示组合可行，false表示不可行
 */
bool pwm_controller_validate_frequency_resolution(uint32_t frequency, uint8_t resolution_bits);

/**
 * @brief 获取当前使用的PWM分辨率位数
 * @return 当前分辨率位数
 */
uint8_t pwm_controller_get_current_resolution(void);

/**
 * @brief 获取当前分辨率下的最大占空比值
 * @return 最大占空比值
 */
uint32_t pwm_controller_get_max_duty_value(void);

#ifdef __cplusplus
}
#endif

#endif // PWM_CONTROLLER_H