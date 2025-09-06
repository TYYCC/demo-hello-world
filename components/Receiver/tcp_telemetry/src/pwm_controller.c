#include "pwm_controller.h"
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "PWM_CONTROLLER";

// 全局PWM控制器实例
static pwm_controller_config_t g_pwm_config = {0};
static pwm_controller_stats_t g_pwm_stats = {0};
static bool g_pwm_initialized = false;
static uint8_t g_current_resolution_bits = PWM_RESOLUTION_BITS; // 当前使用的分辨率位数

// GPIO引脚映射表
static const uint8_t gpio_pins[PWM_CHANNEL_COUNT] = {
    PWM_GPIO_PIN_1, PWM_GPIO_PIN_2, PWM_GPIO_PIN_3, PWM_GPIO_PIN_4,
    PWM_GPIO_PIN_5, PWM_GPIO_PIN_6, PWM_GPIO_PIN_7, PWM_GPIO_PIN_8
};

// 内部函数声明
static esp_err_t pwm_controller_configure_timer(uint32_t frequency);
static esp_err_t pwm_controller_configure_channels(void);
static uint32_t pwm_controller_percent_to_duty(float percent);
static uint64_t pwm_controller_get_timestamp_ms(void);

/**
 * @brief 获取当前时间戳(毫秒)
 */
static uint64_t pwm_controller_get_timestamp_ms(void) {
    return esp_timer_get_time() / 1000;
}

/**
 * @brief 将百分比转换为占空比值
 */
static uint32_t pwm_controller_percent_to_duty(float percent) {
    if (percent < 0.0f) percent = 0.0f;
    if (percent > PWM_MAX_DUTY_PERCENT) percent = PWM_MAX_DUTY_PERCENT;
    
    // 使用当前分辨率计算最大占空比值
    uint32_t max_duty_value = (1 << g_current_resolution_bits) - 1;
    return (uint32_t)((percent / 100.0f) * max_duty_value);
}

/**
 * @brief 配置LEDC定时器
 */
static esp_err_t pwm_controller_configure_timer(uint32_t frequency) {
    // 自动计算最佳分辨率
    uint8_t optimal_resolution = pwm_controller_calculate_optimal_resolution(frequency);
    
    ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = (ledc_timer_bit_t)optimal_resolution,
        .freq_hz = frequency,
        .clk_cfg = LEDC_AUTO_CLK
    };
    
    esp_err_t ret = ledc_timer_config(&timer_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC定时器配置失败: %s (频率: %luHz, 分辨率: %d位)", 
                 esp_err_to_name(ret), frequency, optimal_resolution);
        return ret;
    }
    
    g_pwm_config.timer_num = LEDC_TIMER_0;
    g_pwm_config.speed_mode = LEDC_LOW_SPEED_MODE;
    g_pwm_config.frequency = frequency;
    g_current_resolution_bits = optimal_resolution; // 更新全局分辨率
    
    ESP_LOGI(TAG, "LEDC定时器配置成功，频率: %luHz, 分辨率: %d位", frequency, optimal_resolution);
    return ESP_OK;
}

/**
 * @brief 配置PWM通道
 */
static esp_err_t pwm_controller_configure_channels(void) {
    esp_err_t ret = ESP_OK;
    
    for (int i = 0; i < PWM_CHANNEL_COUNT; i++) {
        // 配置通道参数
        g_pwm_config.channels[i].channel = i;
        g_pwm_config.channels[i].gpio_pin = gpio_pins[i];
        g_pwm_config.channels[i].frequency = g_pwm_config.frequency;
        g_pwm_config.channels[i].duty_value = 0;
        g_pwm_config.channels[i].duty_percent = 0.0f;
        g_pwm_config.channels[i].enabled = true;
        
        // LEDC通道配置
        ledc_channel_config_t channel_config = {
            .speed_mode = g_pwm_config.speed_mode,
            .channel = (ledc_channel_t)i,
            .timer_sel = g_pwm_config.timer_num,
            .intr_type = LEDC_INTR_DISABLE,
            .gpio_num = gpio_pins[i],
            .duty = 0,
            .hpoint = 0
        };
        
        ret = ledc_channel_config(&channel_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "通道%d配置失败: %s", i, esp_err_to_name(ret));
            return ret;
        }
        
        ESP_LOGI(TAG, "PWM通道%d配置成功，GPIO: %d", i, gpio_pins[i]);
    }
    
    return ESP_OK;
}

esp_err_t pwm_controller_init(uint32_t frequency) {
    if (g_pwm_initialized) {
        ESP_LOGW(TAG, "PWM控制器已初始化");
        return ESP_OK;
    }
    
    // 清零配置和统计信息
    memset(&g_pwm_config, 0, sizeof(g_pwm_config));
    memset(&g_pwm_stats, 0, sizeof(g_pwm_stats));
    
    // 设置默认频率
    if (frequency == 0) {
        frequency = PWM_DEFAULT_FREQUENCY;
    }
    
    ESP_LOGI(TAG, "初始化PWM控制器，频率: %lu Hz", frequency);
    
    // 配置定时器
    esp_err_t ret = pwm_controller_configure_timer(frequency);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // 配置通道
    ret = pwm_controller_configure_channels();
    if (ret != ESP_OK) {
        return ret;
    }
    
    g_pwm_config.initialized = true;
    g_pwm_initialized = true;
    
    ESP_LOGI(TAG, "PWM控制器初始化完成");
    return ESP_OK;
}

esp_err_t pwm_controller_deinit(void) {
    if (!g_pwm_initialized) {
        ESP_LOGW(TAG, "PWM控制器未初始化");
        return ESP_OK;
    }
    
    // 停止所有PWM输出
    pwm_controller_stop_all();
    
    // 重置状态
    g_pwm_initialized = false;
    g_pwm_config.initialized = false;
    
    ESP_LOGI(TAG, "PWM控制器反初始化完成");
    return ESP_OK;
}

esp_err_t pwm_controller_set_frequency(uint32_t frequency) {
    if (!g_pwm_initialized) {
        ESP_LOGE(TAG, "PWM控制器未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (frequency == 0) {
        ESP_LOGE(TAG, "频率不能为0");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "设置PWM频率: %lu Hz -> %lu Hz", g_pwm_config.frequency, frequency);
    
    // 重新配置定时器
    esp_err_t ret = ledc_set_freq(g_pwm_config.speed_mode, g_pwm_config.timer_num, frequency);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "设置频率失败: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 更新配置
    g_pwm_config.frequency = frequency;
    for (int i = 0; i < PWM_CHANNEL_COUNT; i++) {
        g_pwm_config.channels[i].frequency = frequency;
    }
    
    // 更新统计信息
    g_pwm_stats.frequency_changes++;
    g_pwm_stats.last_update_time = pwm_controller_get_timestamp_ms();
    
    ESP_LOGI(TAG, "PWM频率设置成功: %lu Hz", frequency);
    return ESP_OK;
}

esp_err_t pwm_controller_set_duty(uint8_t channel, float duty_percent) {
    if (!g_pwm_initialized) {
        ESP_LOGE(TAG, "PWM控制器未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (channel >= PWM_CHANNEL_COUNT) {
        ESP_LOGE(TAG, "通道号无效: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!g_pwm_config.channels[channel].enabled) {
        ESP_LOGD(TAG, "通道%d已禁用", channel);
        return ESP_OK;
    }
    
    // 限制占空比范围
    if (duty_percent < 0.0f) duty_percent = 0.0f;
    if (duty_percent > PWM_MAX_DUTY_PERCENT) duty_percent = PWM_MAX_DUTY_PERCENT;
    
    uint32_t duty_value = pwm_controller_percent_to_duty(duty_percent);
    
    // 设置占空比
    esp_err_t ret = ledc_set_duty(g_pwm_config.speed_mode, (ledc_channel_t)channel, duty_value);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "设置通道%d占空比失败: %s", channel, esp_err_to_name(ret));
        return ret;
    }
    
    // 更新占空比
    ret = ledc_update_duty(g_pwm_config.speed_mode, (ledc_channel_t)channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "更新通道%d占空比失败: %s", channel, esp_err_to_name(ret));
        return ret;
    }
    
    // 更新配置
    g_pwm_config.channels[channel].duty_value = duty_value;
    g_pwm_config.channels[channel].duty_percent = duty_percent;
    
    // 更新统计信息
    g_pwm_stats.total_updates++;
    g_pwm_stats.last_update_time = pwm_controller_get_timestamp_ms();
    
    ESP_LOGD(TAG, "通道%d占空比设置成功: %.2f%% (值: %lu)", channel, duty_percent, duty_value);
    return ESP_OK;
}

esp_err_t pwm_controller_set_channels(const uint16_t *channel_values, uint8_t channel_count) {
    if (!g_pwm_initialized) {
        ESP_LOGE(TAG, "PWM控制器未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!channel_values) {
        ESP_LOGE(TAG, "通道值数组为空");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "批量设置PWM通道，通道数: %d", channel_count);
    
    // 首先将所有通道设为0%
    for (int i = 0; i < PWM_CHANNEL_COUNT; i++) {
        esp_err_t ret = pwm_controller_set_duty(i, 0.0f);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "清零通道%d失败", i);
        }
    }
    
    // 设置有效通道的占空比
    uint8_t max_channels = (channel_count > PWM_CHANNEL_COUNT) ? PWM_CHANNEL_COUNT : channel_count;
    
    for (int i = 0; i < max_channels; i++) {
        // 将0-1000映射到0-96%
        float duty_percent = (channel_values[i] / 1000.0f) * PWM_MAX_DUTY_PERCENT;
        
        esp_err_t ret = pwm_controller_set_duty(i, duty_percent);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "设置通道%d失败", i);
        } else {
            ESP_LOGI(TAG, "通道%d: 值=%d, 占空比=%.2f%%", i + 1, channel_values[i], duty_percent);
        }
    }
    
    return ESP_OK;
}

esp_err_t pwm_controller_enable_channel(uint8_t channel, bool enabled) {
    if (!g_pwm_initialized) {
        ESP_LOGE(TAG, "PWM控制器未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (channel >= PWM_CHANNEL_COUNT) {
        ESP_LOGE(TAG, "通道号无效: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }
    
    g_pwm_config.channels[channel].enabled = enabled;
    
    if (!enabled) {
        // 禁用通道时设置占空比为0
        pwm_controller_set_duty(channel, 0.0f);
    }
    
    ESP_LOGI(TAG, "通道%d %s", channel, enabled ? "启用" : "禁用");
    return ESP_OK;
}

esp_err_t pwm_controller_stop_all(void) {
    if (!g_pwm_initialized) {
        ESP_LOGW(TAG, "PWM控制器未初始化");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "停止所有PWM输出");
    
    for (int i = 0; i < PWM_CHANNEL_COUNT; i++) {
        ledc_stop(g_pwm_config.speed_mode, (ledc_channel_t)i, 0);
        g_pwm_config.channels[i].duty_value = 0;
        g_pwm_config.channels[i].duty_percent = 0.0f;
    }
    
    return ESP_OK;
}

const pwm_controller_config_t* pwm_controller_get_config(void) {
    return &g_pwm_config;
}

const pwm_controller_stats_t* pwm_controller_get_stats(void) {
    return &g_pwm_stats;
}

void pwm_controller_print_status(void) {
    if (!g_pwm_initialized) {
        ESP_LOGI(TAG, "PWM控制器未初始化");
        return;
    }
    
    ESP_LOGI(TAG, "=== PWM控制器状态 ===");
    ESP_LOGI(TAG, "频率: %lu Hz", g_pwm_config.frequency);
    ESP_LOGI(TAG, "定时器: %d, 速度模式: %d", g_pwm_config.timer_num, g_pwm_config.speed_mode);
    
    for (int i = 0; i < PWM_CHANNEL_COUNT; i++) {
        const pwm_channel_config_t *ch = &g_pwm_config.channels[i];
        ESP_LOGI(TAG, "通道%d: GPIO%d, 占空比=%.2f%%, 值=%lu, %s", 
                 i, ch->gpio_pin, ch->duty_percent, ch->duty_value,
                 ch->enabled ? "启用" : "禁用");
    }
    
    ESP_LOGI(TAG, "统计: 总更新=%lu, 频率变更=%lu, 最后更新=%llu ms",
             g_pwm_stats.total_updates, g_pwm_stats.frequency_changes, g_pwm_stats.last_update_time);
}

void pwm_controller_reset_stats(void) {
    memset(&g_pwm_stats, 0, sizeof(pwm_controller_stats_t));
    ESP_LOGI(TAG, "PWM统计信息已重置");
}

/**
 * @brief 根据频率自动计算最佳PWM分辨率
 */
uint8_t pwm_controller_calculate_optimal_resolution(uint32_t frequency) {
    // ESP32的LEDC时钟源通常为80MHz
    const uint32_t ledc_clock_freq = 80000000; // 80MHz
    
    // 从最高分辨率开始尝试，找到第一个可行的分辨率
    for (uint8_t bits = 20; bits >= 1; bits--) {
        uint32_t max_duty_cycles = (1 << bits);
        uint32_t required_clock = frequency * max_duty_cycles;
        
        // 检查是否在LEDC时钟范围内
        if (required_clock <= ledc_clock_freq) {
            ESP_LOGI(TAG, "频率%luHz的最佳分辨率: %d位 (占空比级数: %lu)", 
                     frequency, bits, max_duty_cycles);
            return bits;
        }
    }
    
    // 如果都不可行，返回最小分辨率
    ESP_LOGW(TAG, "频率%luHz过高，使用最小分辨率1位", frequency);
    return 1;
}

/**
 * @brief 验证频率和分辨率组合是否可行
 */
bool pwm_controller_validate_frequency_resolution(uint32_t frequency, uint8_t resolution_bits) {
    if (resolution_bits < 1 || resolution_bits > 20) {
        ESP_LOGE(TAG, "分辨率位数%d超出范围(1-20)", resolution_bits);
        return false;
    }
    
    if (frequency == 0) {
        ESP_LOGE(TAG, "频率不能为0");
        return false;
    }
    
    // ESP32的LEDC时钟源通常为80MHz
    const uint32_t ledc_clock_freq = 80000000; // 80MHz
    uint32_t max_duty_cycles = (1 << resolution_bits);
    uint32_t required_clock = frequency * max_duty_cycles;
    
    bool is_valid = (required_clock <= ledc_clock_freq);
    
    if (is_valid) {
        ESP_LOGI(TAG, "频率%luHz + %d位分辨率: 可行 (需要时钟: %luHz)", 
                 frequency, resolution_bits, required_clock);
    } else {
        ESP_LOGW(TAG, "频率%luHz + %d位分辨率: 不可行 (需要时钟: %luHz > 最大: %luHz)", 
                 frequency, resolution_bits, required_clock, ledc_clock_freq);
    }
    
    return is_valid;
}

/**
 * @brief 获取当前使用的PWM分辨率位数
 */
uint8_t pwm_controller_get_current_resolution(void) {
    return g_current_resolution_bits;
}

/**
 * @brief 获取当前分辨率下的最大占空比值
 */
uint32_t pwm_controller_get_max_duty_value(void) {
    return (1 << g_current_resolution_bits) - 1;
}