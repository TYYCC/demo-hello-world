/**
 * @file lsm6ds3.h
 * @brief LSM6DS3 6轴IMU传感器驱动 (加速度计 + 陀螺仪)
 * @author Your Name
 * @date 2024
 */

#ifndef LSM6DS3_H
#define LSM6DS3_H

#ifdef __cplusplus
extern "C" {
#endif

#include "bsp_i2c.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"

// ========================================
// 硬件连接配置 (可根据实际连接修改)
// ========================================
// I2C pin definitions are now in bsp_i2c.h

#define LSM6DS3_SPI_HOST SPI3_HOST // 使用SPI3
#define LSM6DS3_SPI_MOSI_PIN 35    // 使用不同引脚避免冲突
#define LSM6DS3_SPI_MISO_PIN 37
#define LSM6DS3_SPI_SCLK_PIN 36
#define LSM6DS3_SPI_CS_PIN 34
#define LSM6DS3_SPI_CLOCK_HZ 10000000 // 10MHz

// 通信模式选择 - 推荐使用I2C模式避免SPI冲突
#define LSM6DS3_USE_I2C 1 // 1=I2C, 0=SPI

// ========================================
// LSM6DS3 寄存器地址定义
// ========================================
#define LSM6DS3_REG_FUNC_CFG_ACCESS 0x01
#define LSM6DS3_REG_SENSOR_SYNC_TIME 0x04
#define LSM6DS3_REG_SENSOR_SYNC_RES_RATIO 0x05
#define LSM6DS3_REG_FIFO_CTRL1 0x06
#define LSM6DS3_REG_FIFO_CTRL2 0x07
#define LSM6DS3_REG_FIFO_CTRL3 0x08
#define LSM6DS3_REG_FIFO_CTRL4 0x09
#define LSM6DS3_REG_FIFO_CTRL5 0x0A
#define LSM6DS3_REG_DRDY_PULSE_CFG 0x0B
#define LSM6DS3_REG_INT1_CTRL 0x0D
#define LSM6DS3_REG_INT2_CTRL 0x0E
#define LSM6DS3_REG_WHO_AM_I 0x0F
#define LSM6DS3_REG_CTRL1_XL 0x10
#define LSM6DS3_REG_CTRL2_G 0x11
#define LSM6DS3_REG_CTRL3_C 0x12
#define LSM6DS3_REG_CTRL4_C 0x13
#define LSM6DS3_REG_CTRL5_C 0x14
#define LSM6DS3_REG_CTRL6_C 0x15
#define LSM6DS3_REG_CTRL7_G 0x16
#define LSM6DS3_REG_CTRL8_XL 0x17
#define LSM6DS3_REG_CTRL9_XL 0x18
#define LSM6DS3_REG_CTRL10_C 0x19
#define LSM6DS3_REG_MASTER_CONFIG 0x1A
#define LSM6DS3_REG_WAKE_UP_SRC 0x1B
#define LSM6DS3_REG_TAP_SRC 0x1C
#define LSM6DS3_REG_D6D_SRC 0x1D
#define LSM6DS3_REG_STATUS_REG 0x1E
#define LSM6DS3_REG_OUT_TEMP_L 0x20
#define LSM6DS3_REG_OUT_TEMP_H 0x21
#define LSM6DS3_REG_OUTX_L_G 0x22
#define LSM6DS3_REG_OUTX_H_G 0x23
#define LSM6DS3_REG_OUTY_L_G 0x24
#define LSM6DS3_REG_OUTY_H_G 0x25
#define LSM6DS3_REG_OUTZ_L_G 0x26
#define LSM6DS3_REG_OUTZ_H_G 0x27
#define LSM6DS3_REG_OUTX_L_XL 0x28
#define LSM6DS3_REG_OUTX_H_XL 0x29
#define LSM6DS3_REG_OUTY_L_XL 0x2A
#define LSM6DS3_REG_OUTY_H_XL 0x2B
#define LSM6DS3_REG_OUTZ_L_XL 0x2C
#define LSM6DS3_REG_OUTZ_H_XL 0x2D

// ========================================
// 寄存器位定义
// ========================================
// WHO_AM_I 寄存器
#define LSM6DS3_WHO_AM_I_VALUE 0x69

// CTRL1_XL 寄存器位定义
#define LSM6DS3_CTRL1_XL_ODR_XL_MASK 0xF0
#define LSM6DS3_CTRL1_XL_FS_XL_MASK 0x0C
#define LSM6DS3_CTRL1_XL_BW_XL_MASK 0x03

// CTRL2_G 寄存器位定义
#define LSM6DS3_CTRL2_G_ODR_G_MASK 0xF0
#define LSM6DS3_CTRL2_G_FS_G_MASK 0x0C
#define LSM6DS3_CTRL2_G_FS_125_MASK 0x02

// CTRL3_C 寄存器位定义
#define LSM6DS3_CTRL3_C_SW_RESET 0x01
#define LSM6DS3_CTRL3_C_IF_INC 0x04
#define LSM6DS3_CTRL3_C_SIM 0x08
#define LSM6DS3_CTRL3_C_PP_OD 0x10
#define LSM6DS3_CTRL3_C_H_LACTIVE 0x20
#define LSM6DS3_CTRL3_C_BDU 0x40
#define LSM6DS3_CTRL3_C_BOOT 0x80

// ========================================
// 输出数据率 (ODR) 定义
// ========================================
#define LSM6DS3_ODR_POWER_DOWN 0x00
#define LSM6DS3_ODR_12_5_HZ 0x10
#define LSM6DS3_ODR_26_HZ 0x20
#define LSM6DS3_ODR_52_HZ 0x30
#define LSM6DS3_ODR_104_HZ 0x40
#define LSM6DS3_ODR_208_HZ 0x50
#define LSM6DS3_ODR_416_HZ 0x60
#define LSM6DS3_ODR_833_HZ 0x70
#define LSM6DS3_ODR_1660_HZ 0x80
#define LSM6DS3_ODR_3330_HZ 0x90
#define LSM6DS3_ODR_6660_HZ 0xA0

// ========================================
// 加速度计满量程范围定义
// ========================================
#define LSM6DS3_ACCEL_FS_2G 0x00
#define LSM6DS3_ACCEL_FS_4G 0x04
#define LSM6DS3_ACCEL_FS_8G 0x08
#define LSM6DS3_ACCEL_FS_16G 0x0C

// ========================================
// 陀螺仪满量程范围定义
// ========================================
#define LSM6DS3_GYRO_FS_125DPS 0x02
#define LSM6DS3_GYRO_FS_250DPS 0x00
#define LSM6DS3_GYRO_FS_500DPS 0x04
#define LSM6DS3_GYRO_FS_1000DPS 0x08
#define LSM6DS3_GYRO_FS_2000DPS 0x0C

// ========================================
// 数据结构定义
// ========================================
typedef struct {
    float x;
    float y;
    float z;
} lsm6ds3_accel_data_t;

typedef struct {
    float x;
    float y;
    float z;
} lsm6ds3_gyro_data_t;

typedef struct {
    float temperature;
} lsm6ds3_temp_data_t;

typedef struct {
    lsm6ds3_accel_data_t accel;
    lsm6ds3_gyro_data_t gyro;
    lsm6ds3_temp_data_t temp;
} lsm6ds3_data_t;

/**
 * @brief 欧拉角 (单位: 度)
 */
typedef struct {
    float roll;  // 横滚
    float pitch; // 俯仰
    float yaw;   // 航向
} lsm6ds3_euler_t;

/**
 * @brief 通信模式
 */
typedef enum {
    LSM6DS3_COMM_MODE_I2C,
    LSM6DS3_COMM_MODE_SPI,
} lsm6ds3_comm_mode_t;

// ========================================
// 结构体和枚举
// ========================================

/**
 * @brief LSM6DS3设备句柄
 */
typedef struct {
    lsm6ds3_comm_mode_t comm_mode;          // 通信模式 (I2C/SPI)
    i2c_port_t i2c_port;                    // I2C端口号
    i2c_master_bus_handle_t i2c_bus_handle; // I2C总线句柄
    i2c_master_dev_handle_t i2c_dev_handle; // I2C设备句柄
    spi_host_device_t spi_host;             // SPI主机
    spi_device_handle_t spi_handle;         // SPI设备句柄
    uint8_t accel_fs;                       // 加速度计量程
    uint8_t gyro_fs;                        // 陀螺仪量程
    bool is_initialized;                    // 是否已初始化
} lsm6ds3_handle_t;

// ========================================
// 函数声明
// ========================================

/**
 * @brief 初始化LSM6DS3传感器
 * @return ESP_OK 成功, 其他值表示错误
 */
esp_err_t lsm6ds3_init(void);

/**
 * @brief 反初始化LSM6DS3传感器
 * @return ESP_OK 成功, 其他值表示错误
 */
esp_err_t lsm6ds3_deinit(void);

/**
 * @brief 配置加速度计
 * @param odr 输出数据率
 * @param fs 满量程范围
 * @return ESP_OK 成功, 其他值表示错误
 */
esp_err_t lsm6ds3_config_accel(uint8_t odr, uint8_t fs);

/**
 * @brief 配置陀螺仪
 * @param odr 输出数据率
 * @param fs 满量程范围
 * @return ESP_OK 成功, 其他值表示错误
 */
esp_err_t lsm6ds3_config_gyro(uint8_t odr, uint8_t fs);

/**
 * @brief 读取加速度计数据
 * @param accel_data 加速度计数据指针
 * @return ESP_OK 成功, 其他值表示错误
 */
esp_err_t lsm6ds3_read_accel(lsm6ds3_accel_data_t* accel_data);

/**
 * @brief 读取陀螺仪数据
 * @param gyro_data 陀螺仪数据指针
 * @return ESP_OK 成功, 其他值表示错误
 */
esp_err_t lsm6ds3_read_gyro(lsm6ds3_gyro_data_t* gyro_data);

/**
 * @brief 读取温度数据
 * @param temp_data 温度数据指针
 * @return ESP_OK 成功, 其他值表示错误
 */
esp_err_t lsm6ds3_read_temp(lsm6ds3_temp_data_t* temp_data);

/**
 * @brief 读取所有传感器数据
 * @param data 传感器数据指针
 * @return ESP_OK 成功, 其他值表示错误
 */
esp_err_t lsm6ds3_read_all(lsm6ds3_data_t* data);

/**
 * @brief 使用 Fusion AHRS 解算并返回欧拉角 (无磁力计)
 *        内部会读取一次加速度计和陀螺仪，并根据两次调用间隔动态计算 deltaTime。
 * @param euler 欧拉角输出指针 (单位: 度)
 * @return ESP_OK 成功, 其他值表示错误
 */
esp_err_t lsm6ds3_read_euler(lsm6ds3_euler_t* euler);

/**
 * @brief 检查传感器是否就绪
 * @return true 就绪, false 未就绪
 */
bool lsm6ds3_is_ready(void);

/**
 * @brief 软复位传感器
 * @return ESP_OK 成功, 其他值表示错误
 */
esp_err_t lsm6ds3_reset(void);

/**
 * @brief 启用/禁用加速度计
 * @param enable true=启用, false=禁用
 * @return ESP_OK 成功, 其他值表示错误
 */
esp_err_t lsm6ds3_accel_enable(bool enable);

/**
 * @brief 启用/禁用陀螺仪
 * @param enable true=启用, false=禁用
 * @return ESP_OK 成功, 其他值表示错误
 */
esp_err_t lsm6ds3_gyro_enable(bool enable);

/**
 * @brief 检查SPI引脚冲突
 * @return true 有冲突, false 无冲突
 */
bool lsm6ds3_check_spi_conflict(void);

#ifdef __cplusplus
}
#endif

#endif // LSM6DS3_H