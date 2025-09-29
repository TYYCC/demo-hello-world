#ifndef KEY_H
#define KEY_H

#include "driver/gpio.h"
#include <stdint.h>

#define KEY_SH_LD_Pin 4
#define KEY_SCK_Pin 6
#define KEY_INPUT_Pin 7

#define KEY_SH_LD_L gpio_set_level(KEY_SH_LD_Pin, 0)
#define KEY_SH_LD_H gpio_set_level(KEY_SH_LD_Pin, 1)
#define KEY_SCK_L gpio_set_level(KEY_SCK_Pin, 0)
#define KEY_SCK_H gpio_set_level(KEY_SCK_Pin, 1)
#define KEY_INPUT gpio_get_level(KEY_INPUT_Pin)

typedef enum {
    KEY_NONE = 0x00,
    KEY_UP = 0x01,
    KEY_DOWN = 0x02,
    KEY_LEFT = 0x04,
    KEY_RIGHT = 0x08,
    KEY_CENTER = 0x10,
    KEY_VOL_UP = 0x20,
    KEY_VOL_DOWN = 0x40,

    KEY_UP_LONG = 0x0100,
    KEY_DOWN_LONG = 0x0200,
    KEY_LEFT_LONG = 0x0400,
    KEY_RIGHT_LONG = 0x0800,
    KEY_CENTER_LONG = 0x1000,
    KEY_VOL_UP_LONG = 0x2000,
    KEY_VOL_DOWN_LONG = 0x4000
} key_dir_t;

/**
 * @brief 初始化按键接口（初始化摇杆ADC）
 */
void key_init(void);

/**
 * @brief 扫描摇杆并返回当前按键状态，按键状态可组合多方向
 * @return key_dir_t 方向按键的按位掩码
 */
key_dir_t key_scan(void);

/**
 * @brief 设置是否启用ADC摇杆到按键的转换
 * @param enable true启用，false禁用
 */
void set_adc_key_enable(bool enable);

#endif // KEY_H