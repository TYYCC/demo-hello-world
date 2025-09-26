
#ifndef BSP_I2C_H
#define BSP_I2C_H

#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_I2C_NUM I2C_NUM_0
#define BSP_I2C_SCL_PIN 20
#define BSP_I2C_SDA_PIN 19
#define BSP_I2C_FREQ_HZ 400000

esp_err_t bsp_i2c_init(void);
esp_err_t bsp_i2c_deinit(void);
i2c_master_bus_handle_t bsp_i2c_get_bus_handle(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_I2C_H */
