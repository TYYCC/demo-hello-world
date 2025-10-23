
#ifndef FT6336G_H
#define FT6336G_H

#include "driver/i2c_master.h"
#include "esp_err.h"


#ifdef __cplusplus
extern "C" {
#endif

#define FT6336G_I2C_ADDR 0x38
#define FT6336G_INT_PIN 21

typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t touch_id;
    uint8_t weight;
    uint8_t area;
} ft6336g_touch_point_t;

esp_err_t ft6336g_init(void);
esp_err_t ft6336g_read_touch_points(ft6336g_touch_point_t* points, uint8_t* num_points);
uint8_t ft6336g_get_touch_points(void);

#ifdef __cplusplus
}
#endif

#endif /* FT6336G_H */
