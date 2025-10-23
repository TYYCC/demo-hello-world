/**
 * @file joystick_adc.h
 * @brief 使用ADC采集摇杆数据的驱动
 * @author Your Name
 * @date 2024
 */

#ifndef JOYSTICK_ADC_H
#define JOYSTICK_ADC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "driver/adc.h"

// ========================================
// 摇杆硬件配置
// ========================================
// 摇杆1
#define JOYSTICK1_ADC_X_CHANNEL ADC1_CHANNEL_2 // IO3
#define JOYSTICK1_ADC_Y_CHANNEL ADC1_CHANNEL_8 // IO9

// ADC衰减配置
#define JOYSTICK_ADC_ATTEN      ADC_ATTEN_DB_11

// ADC低通滤波系数 (0.0 < alpha < 1.0)
// 值越小，平滑效果越好，但延迟越高
#define JOYSTICK_LOW_PASS_ALPHA 0.2f

// ========================================
// 数据结构定义
// ========================================

/**
 * @brief 单个摇杆轴的校准数据
 */
typedef struct {
    int min;      // 采集到的最小值
    int max;      // 采集到的最大值
    int center;   // 中心点
} joystick_axis_cal_t;

/**
 * @brief 摇杆的校准数据
 */
typedef struct {
    joystick_axis_cal_t joy1_x;
    joystick_axis_cal_t joy1_y;
} joystick_cal_data_t;

/**
 * @brief 摇杆最终输出数据
 */
typedef struct {
    int raw_joy1_x;     // 摇杆1 X轴原始值
    int raw_joy1_y;     // 摇杆1 Y轴原始值

    int joy1_x_mv;      // 摇杆1 X轴电压 (mV)
    int joy1_y_mv;      // 摇杆1 Y轴电压 (mV)
    
    // 归一化后的输出值 (-100 到 100)
    int norm_joy1_x;
    int norm_joy1_y;
} joystick_data_t;

// ========================================
// 函数声明
// ========================================

/**
 * @brief 初始化ADC用于摇杆数据采集
 *
 * @return
 *     - ESP_OK: 成功
 *     - ESP_FAIL: 初始化失败
 */
esp_err_t joystick_adc_init(void);

/**
 * @brief 反初始化ADC
 *
 * @return
 *     - ESP_OK: 成功
 */
esp_err_t joystick_adc_deinit(void);

/**
 * @brief 从两个摇杆读取经过滤波和校准的数据
 *
 * @param[out] data 指向joystick_data_t结构体的指针，用于存储读取的数据
 * @return
 *     - ESP_OK: 成功
 *     - ESP_ERR_INVALID_ARG: 参数错误
 *     - ESP_ERR_INVALID_STATE: 驱动未初始化
 */
esp_err_t joystick_adc_read(joystick_data_t *data);

/**
 * @brief 开始摇杆校准
 *        会重置当前的校准数据
 */
void joystick_start_calibration(void);

/**
 * @brief 结束摇杆校准
 *        会根据校准过程中记录的最大最小值计算中心点
 */
void joystick_stop_calibration(void);

/**
 * @brief 检查当前是否已经校准
 * @return true - 已校准, false - 未校准
 */
bool joystick_is_calibrated(void);

/**
 * @brief 从NVS加载校准数据
 * @return 
 *      - ESP_OK: 成功加载
 *      - ESP_ERR_NVS_NOT_FOUND: 未找到校准数据
 *      - 其他: NVS错误
 */
esp_err_t joystick_load_calibration_from_nvs(void);

/**
 * @brief 将当前校准数据保存到NVS
 * @return
 *      - ESP_OK: 成功保存
 *      - ESP_ERR_INVALID_STATE: 尚未校准
 *      - 其他: NVS错误
 */
esp_err_t joystick_save_calibration_to_nvs(void);

#ifdef __cplusplus
}
#endif

#endif // JOYSTICK_ADC_H 