#ifndef GT911_H
#define GT911_H

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GT911_I2C_ADDR_LOW 0x5D           // GT911 I2C地址 (ADDR引脚接低电平)
#define GT911_I2C_ADDR_HIGH 0x14          // GT911 I2C地址 (ADDR引脚接高电平)
#define GT911_I2C_ADDR GT911_I2C_ADDR_LOW // 默认使用低电平地址
#define GT911_INT_PIN 21                  // 中断引脚
// #define GT911_RST_PIN       18     // 复位引脚 (如果有的话)

typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t touch_id;
    uint8_t weight;
    uint8_t area;
} gt911_touch_point_t;

// 中断回调函数类型
typedef void (*gt911_irq_callback_t)(void);

esp_err_t gt911_init(void);
esp_err_t gt911_read_touch_points(gt911_touch_point_t* points, uint8_t* num_points);
uint8_t gt911_get_touch_points(void);
void gt911_register_irq_callback(gt911_irq_callback_t callback);
bool gt911_is_irq_triggered(void);
void gt911_clear_irq_flag(void);

#ifdef __cplusplus
}
#endif

#endif /* GT911_H */
