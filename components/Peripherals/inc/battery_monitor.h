/**
 * @file battery_monitor.h
 * @brief 电池电量监测模块
 * @author Your Name
 * @date 2024
 */

#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "driver/adc.h"
#include "esp_err.h"

// ========================================
// 硬件配置
// ========================================
#define BATTERY_ADC_CHANNEL ADC1_CHANNEL_7 // GPIO8
#define BATTERY_ADC_ATTEN ADC_ATTEN_DB_12  // 0-3.3V (使用新的衰减值)

// 电池电压分压比例 (如果使用分压电路)
#define BATTERY_VOLTAGE_DIVIDER_RATIO 2.0f // 根据实际分压电路调整

// 电池电压范围 (mV)
#define BATTERY_VOLTAGE_MIN 3000 // 3.0V
#define BATTERY_VOLTAGE_MAX 4200 // 4.2V

// 校准数据存储
#define BATTERY_NVS_NAMESPACE "battery_cal"
#define BATTERY_NVS_CAL_KEY "cal_data"

// 电量百分比阈值
#define BATTERY_PERCENT_CRITICAL 10 // 10% 以下为严重
#define BATTERY_PERCENT_LOW 20      // 20% 以下为低电量
#define BATTERY_PERCENT_MEDIUM 50   // 50% 以下为中等电量

// 滤波系数
#define BATTERY_FILTER_ALPHA 0.1f // 低通滤波系数 (0.0 < alpha < 1.0)

// ========================================
// 数据结构定义
// ========================================

/**
 * @brief 电池状态枚举
 */
typedef enum {
    BATTERY_STATUS_UNKNOWN = 0,
    BATTERY_STATUS_CHARGING,
    BATTERY_STATUS_DISCHARGING,
    BATTERY_STATUS_FULL,
    BATTERY_STATUS_EMPTY
} battery_status_t;

/**
 * @brief 电池校准数据
 */
typedef struct {
    float voltage_offset; // 电压偏移量 (mV)
    float voltage_scale;  // 电压缩放系数
    int min_voltage_mv;   // 最小电压 (mV)
    int max_voltage_mv;   // 最大电压 (mV)
    bool is_calibrated;   // 是否已校准
} battery_cal_data_t;

/**
 * @brief 电池信息结构体
 */
typedef struct {
    int voltage_mv;          // 电池电压 (mV)
    int percentage;          // 电量百分比 (0-100)
    battery_status_t status; // 电池状态
    bool is_low_battery;     // 是否低电量
    bool is_critical;        // 是否严重低电量
} battery_info_t;

// ========================================
// 函数声明
// ========================================

/**
 * @brief 初始化电池监测模块
 *
 * @return
 *     - ESP_OK: 成功
 *     - ESP_FAIL: 初始化失败
 */
esp_err_t battery_monitor_init(void);

/**
 * @brief 反初始化电池监测模块
 *
 * @return
 *     - ESP_OK: 成功
 */
esp_err_t battery_monitor_deinit(void);

/**
 * @brief 读取电池信息
 *
 * @param[out] info 指向battery_info_t结构体的指针，用于存储电池信息
 * @return
 *     - ESP_OK: 成功
 *     - ESP_ERR_INVALID_ARG: 参数错误
 *     - ESP_ERR_INVALID_STATE: 模块未初始化
 */
esp_err_t battery_monitor_read(battery_info_t* info);

/**
 * @brief 获取电池电压 (mV)
 *
 * @param[out] voltage_mv 电池电压值
 * @return
 *     - ESP_OK: 成功
 *     - ESP_ERR_INVALID_STATE: 模块未初始化
 */
esp_err_t battery_monitor_get_voltage(int* voltage_mv);

/**
 * @brief 获取电池电量百分比
 *
 * @param[out] percentage 电量百分比 (0-100)
 * @return
 *     - ESP_OK: 成功
 *     - ESP_ERR_INVALID_STATE: 模块未初始化
 */
esp_err_t battery_monitor_get_percentage(int* percentage);

/**
 * @brief 检查是否为低电量
 *
 * @return true - 低电量, false - 电量正常
 */
bool battery_monitor_is_low_battery(void);

/**
 * @brief 检查是否为严重低电量
 *
 * @return true - 严重低电量, false - 电量正常
 */
bool battery_monitor_is_critical_battery(void);

// ========================================
// 校准功能函数
// ========================================

/**
 * @brief 开始电池校准
 * 需要提供已知的电池电压值进行校准
 *
 * @param known_voltage_mv 已知的电池电压值 (mV)
 * @return
 *     - ESP_OK: 成功
 *     - ESP_ERR_INVALID_STATE: 模块未初始化
 */
esp_err_t battery_monitor_start_calibration(int known_voltage_mv);

/**
 * @brief 完成电池校准
 * 使用当前ADC读数完成校准
 *
 * @return
 *     - ESP_OK: 成功
 *     - ESP_ERR_INVALID_STATE: 模块未初始化
 */
esp_err_t battery_monitor_finish_calibration(void);

/**
 * @brief 取消电池校准
 *
 * @return
 *     - ESP_OK: 成功
 */
esp_err_t battery_monitor_cancel_calibration(void);

/**
 * @brief 检查是否正在校准
 *
 * @return true - 正在校准, false - 未校准
 */
bool battery_monitor_is_calibrating(void);

/**
 * @brief 检查是否已校准
 *
 * @return true - 已校准, false - 未校准
 */
bool battery_monitor_is_calibrated(void);

/**
 * @brief 从NVS加载校准数据
 *
 * @return
 *     - ESP_OK: 成功加载
 *     - ESP_ERR_NVS_NOT_FOUND: 未找到校准数据
 *     - 其他: NVS错误
 */
esp_err_t battery_monitor_load_calibration_from_nvs(void);

/**
 * @brief 将校准数据保存到NVS
 *
 * @return
 *     - ESP_OK: 成功保存
 *     - ESP_ERR_INVALID_STATE: 尚未校准
 *     - 其他: NVS错误
 */
esp_err_t battery_monitor_save_calibration_to_nvs(void);

/**
 * @brief 重置校准数据
 * 清除所有校准数据，恢复到默认设置
 *
 * @return
 *     - ESP_OK: 成功
 */
esp_err_t battery_monitor_reset_calibration(void);

#ifdef __cplusplus
}
#endif

#endif // BATTERY_MONITOR_H