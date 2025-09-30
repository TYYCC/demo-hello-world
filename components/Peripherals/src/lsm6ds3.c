/**
 * @file lsm6ds3.c
 * @brief LSM6DS3 6轴IMU传感器驱动实现
 * @author Your Name
 * @date 2024
 */

#include "lsm6ds3.h"
#include "bsp_i2c.h" // 包含新的I2C头文件
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

// Fusion AHRS
#include "FusionAhrs.h"
#include "FusionMath.h"
#include "FusionOffset.h"

// ========================================
// 私有变量和常量
// ========================================
static const char* TAG = "LSM6DS3";
static lsm6ds3_handle_t g_lsm6ds3_handle = {0};
static FusionAhrs s_ahrs;          // Fusion AHRS 状态
static FusionOffset s_offset;      // Fusion 零漂补偿算法
static bool s_ahrs_inited = false; // AHRS 是否已初始化
static int64_t s_last_time_us = 0; // 上次更新时间 (us)

// I2C设备地址
#define LSM6DS3_I2C_ADDR 0x6A     // 默认I2C地址 (SDO=GND)
#define LSM6DS3_I2C_ADDR_ALT 0x6B // 备用I2C地址 (SDO=VDD)

// ========================================
// 私有函数声明
// ========================================
static esp_err_t lsm6ds3_i2c_init(void);
static esp_err_t lsm6ds3_spi_init(void);
static esp_err_t lsm6ds3_read_reg(uint8_t reg, uint8_t* data, size_t len);
static esp_err_t lsm6ds3_write_reg(uint8_t reg, uint8_t data);
static esp_err_t lsm6ds3_read_reg_i2c(uint8_t reg, uint8_t* data, size_t len);
static esp_err_t lsm6ds3_write_reg_i2c(uint8_t reg, uint8_t data);
static esp_err_t lsm6ds3_read_reg_spi(uint8_t reg, uint8_t* data, size_t len);
static esp_err_t lsm6ds3_write_reg_spi(uint8_t reg, uint8_t data);
static float lsm6ds3_convert_accel_raw_to_g(int16_t raw, uint8_t fs);
static float lsm6ds3_convert_gyro_raw_to_dps(int16_t raw, uint8_t fs);
static float lsm6ds3_convert_temp_raw_to_celsius(int16_t raw);

// ========================================
// I2C通信函数
// ========================================

/**
 * @brief 初始化I2C接口
 */
static esp_err_t lsm6ds3_i2c_init(void) {
    g_lsm6ds3_handle.i2c_bus_handle = bsp_i2c_get_bus_handle();
    if (g_lsm6ds3_handle.i2c_bus_handle == NULL) {
        ESP_LOGE(TAG, "I2C bus handle not initialized. Call bsp_i2c_init() first.");
        return ESP_FAIL;
    }

    // I2C设备配置
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = LSM6DS3_I2C_ADDR_ALT,
        .scl_speed_hz = BSP_I2C_FREQ_HZ,
    };
    esp_err_t ret = i2c_master_bus_add_device(g_lsm6ds3_handle.i2c_bus_handle, &dev_cfg,
                                              &g_lsm6ds3_handle.i2c_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C master bus add device failed: %s", esp_err_to_name(ret));
        return ret;
    }

    g_lsm6ds3_handle.i2c_port = BSP_I2C_NUM;
    ESP_LOGI(TAG, "LSM6DS3 I2C device added successfully");
    return ESP_OK;
}

/**
 * @brief I2C读取寄存器
 */
static esp_err_t lsm6ds3_read_reg_i2c(uint8_t reg, uint8_t* data, size_t len) {
    esp_err_t ret = i2c_master_transmit_receive(g_lsm6ds3_handle.i2c_dev_handle, &reg, 1, data, len,
                                                pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

/**
 * @brief I2C写入寄存器
 */
static esp_err_t lsm6ds3_write_reg_i2c(uint8_t reg, uint8_t data) {
    esp_err_t ret;
    uint8_t write_buf[2] = {reg, data};

    ret = i2c_master_transmit(g_lsm6ds3_handle.i2c_dev_handle, write_buf, sizeof(write_buf),
                              pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C write failed: %s", esp_err_to_name(ret));
    }

    return ret;
}

// ========================================
// SPI通信函数
// ========================================

/**
 * @brief 初始化SPI接口
 */
static esp_err_t lsm6ds3_spi_init(void) {
    esp_err_t ret;

    // SPI总线配置
    spi_bus_config_t bus_config = {
        .mosi_io_num = LSM6DS3_SPI_MOSI_PIN,
        .miso_io_num = LSM6DS3_SPI_MISO_PIN,
        .sclk_io_num = LSM6DS3_SPI_SCLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32,
    };

    // 初始化SPI总线
    ret = spi_bus_initialize(LSM6DS3_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus initialize failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // SPI设备配置
    spi_device_interface_config_t dev_config = {
        .clock_speed_hz = LSM6DS3_SPI_CLOCK_HZ,
        .mode = 3, // SPI模式3
        .spics_io_num = LSM6DS3_SPI_CS_PIN,
        .queue_size = 7,
        .pre_cb = NULL,
        .post_cb = NULL,
    };

    // 添加SPI设备
    ret = spi_bus_add_device(LSM6DS3_SPI_HOST, &dev_config, &g_lsm6ds3_handle.spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(ret));
        spi_bus_free(LSM6DS3_SPI_HOST);
        return ret;
    }

    ESP_LOGI(TAG, "SPI initialized successfully");
    return ESP_OK;
}

/**
 * @brief SPI读取寄存器
 */
static esp_err_t lsm6ds3_read_reg_spi(uint8_t reg, uint8_t* data, size_t len) {
    esp_err_t ret;
    spi_transaction_t trans = {0};

    // 设置读取位
    reg |= 0x80;

    trans.length = (len + 1) * 8; // 总位数
    trans.tx_data[0] = reg;
    trans.rxlength = (len + 1) * 8;

    ret = spi_device_transmit(g_lsm6ds3_handle.spi_handle, &trans);
    if (ret == ESP_OK) {
        memcpy(data, &trans.rx_data[1], len);
    } else {
        ESP_LOGE(TAG, "SPI read failed: %s", esp_err_to_name(ret));
    }

    return ret;
}

/**
 * @brief SPI写入寄存器
 */
static esp_err_t lsm6ds3_write_reg_spi(uint8_t reg, uint8_t data) {
    esp_err_t ret;
    spi_transaction_t trans = {0};

    // 清除读取位
    reg &= 0x7F;

    trans.length = 16; // 2字节
    trans.tx_data[0] = reg;
    trans.tx_data[1] = data;

    ret = spi_device_transmit(g_lsm6ds3_handle.spi_handle, &trans);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI write failed: %s", esp_err_to_name(ret));
    }

    return ret;
}

// ========================================
// 通用寄存器读写函数
// ========================================

/**
 * @brief 读取寄存器
 */
static esp_err_t lsm6ds3_read_reg(uint8_t reg, uint8_t* data, size_t len) {
#if LSM6DS3_USE_I2C
    return lsm6ds3_read_reg_i2c(reg, data, len);
#else
    return lsm6ds3_read_reg_spi(reg, data, len);
#endif
}

/**
 * @brief 写入寄存器
 */
static esp_err_t lsm6ds3_write_reg(uint8_t reg, uint8_t data) {
#if LSM6DS3_USE_I2C
    return lsm6ds3_write_reg_i2c(reg, data);
#else
    return lsm6ds3_write_reg_spi(reg, data);
#endif
}

// ========================================
// 数据转换函数
// ========================================

/**
 * @brief 将加速度计原始数据转换为g值
 */
static float lsm6ds3_convert_accel_raw_to_g(int16_t raw, uint8_t fs) {
    float scale;
    switch (fs) {
    case LSM6DS3_ACCEL_FS_2G:
        scale = 2.0f / 32768.0f;
        break;
    case LSM6DS3_ACCEL_FS_4G:
        scale = 4.0f / 32768.0f;
        break;
    case LSM6DS3_ACCEL_FS_8G:
        scale = 8.0f / 32768.0f;
        break;
    case LSM6DS3_ACCEL_FS_16G:
        scale = 16.0f / 32768.0f;
        break;
    default:
        scale = 2.0f / 32768.0f;
        break;
    }
    return (float)raw * scale;
}

/**
 * @brief 将陀螺仪原始数据转换为度/秒
 */
static float lsm6ds3_convert_gyro_raw_to_dps(int16_t raw, uint8_t fs) {
    float scale;
    switch (fs) {
    case LSM6DS3_GYRO_FS_125DPS:
        scale = 125.0f / 32768.0f;
        break;
    case LSM6DS3_GYRO_FS_250DPS:
        scale = 250.0f / 32768.0f;
        break;
    case LSM6DS3_GYRO_FS_500DPS:
        scale = 500.0f / 32768.0f;
        break;
    case LSM6DS3_GYRO_FS_1000DPS:
        scale = 1000.0f / 32768.0f;
        break;
    case LSM6DS3_GYRO_FS_2000DPS:
        scale = 2000.0f / 32768.0f;
        break;
    default:
        scale = 250.0f / 32768.0f;
        break;
    }
    return (float)raw * scale;
}

/**
 * @brief 将温度原始数据转换为摄氏度
 */
static float lsm6ds3_convert_temp_raw_to_celsius(int16_t raw) {
    return (float)raw / 256.0f + 25.0f;
}

// ========================================
// 公共函数实现
// ========================================

/**
 * @brief 初始化LSM6DS3传感器
 */
esp_err_t lsm6ds3_init(void) {
    esp_err_t ret;
    uint8_t who_am_i;

    if (g_lsm6ds3_handle.is_initialized) {
        ESP_LOGW(TAG, "LSM6DS3 already initialized");
        return ESP_OK;
    }

    // 初始化通信接口
#if LSM6DS3_USE_I2C
    ret = lsm6ds3_i2c_init();
#else
    ret = lsm6ds3_spi_init();
#endif

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Communication interface init failed");
        return ret;
    }

    // 读取WHO_AM_I寄存器验证设备
    ret = lsm6ds3_read_reg(LSM6DS3_REG_WHO_AM_I, &who_am_i, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read WHO_AM_I register");
        return ret;
    }

    if (who_am_i != LSM6DS3_WHO_AM_I_VALUE) {
        ESP_LOGE(TAG, "WHO_AM_I mismatch: expected 0x%02X, got 0x%02X", LSM6DS3_WHO_AM_I_VALUE,
                 who_am_i);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "LSM6DS3 found, WHO_AM_I: 0x%02X", who_am_i);

    // 软复位
    ret = lsm6ds3_reset();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Soft reset failed");
        return ret;
    }

    // 等待复位完成
    vTaskDelay(pdMS_TO_TICKS(100));

    // 配置CTRL3_C寄存器
    uint8_t ctrl3_c = LSM6DS3_CTRL3_C_IF_INC | LSM6DS3_CTRL3_C_BDU;
    ret = lsm6ds3_write_reg(LSM6DS3_REG_CTRL3_C, ctrl3_c);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure CTRL3_C");
        return ret;
    }

    // 设置默认满量程范围
    g_lsm6ds3_handle.accel_fs = LSM6DS3_ACCEL_FS_2G;
    g_lsm6ds3_handle.gyro_fs = LSM6DS3_GYRO_FS_250DPS;
    
    // 配置更高的采样率以提高精度（416Hz）
    ret = lsm6ds3_config_accel(LSM6DS3_ODR_416_HZ, LSM6DS3_ACCEL_FS_2G);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to configure accelerometer ODR");
    }
    
    ret = lsm6ds3_config_gyro(LSM6DS3_ODR_416_HZ, LSM6DS3_GYRO_FS_250DPS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to configure gyroscope ODR");
    }
    
    // 启用传感器
    lsm6ds3_accel_enable(true);
    lsm6ds3_gyro_enable(true);

    g_lsm6ds3_handle.is_initialized = true;
    ESP_LOGI(TAG, "LSM6DS3 initialized successfully with 416Hz ODR");

    // 初始化 Fusion AHRS
    FusionAhrsInitialise(&s_ahrs);
    
    // 配置 AHRS 参数以提高精度和减少漂移
    const FusionAhrsSettings settings = {
        .convention = FusionConventionNwu,           // 坐标系：北-西-上
        .gain = 0.5f,                                 // 增益：平衡陀螺仪和加速度计 (0.5 是适中值)
        .gyroscopeRange = 250.0f,                     // 陀螺仪量程 (dps)
        .accelerationRejection = 10.0f,               // 加速度拒绝阈值 (g)
        .magneticRejection = 0.0f,                    // 磁力拒绝（无磁力计时设为0）
        .recoveryTriggerPeriod = 5 * 416,             // 恢复触发周期 (5秒 @ 416Hz)
    };
    FusionAhrsSetSettings(&s_ahrs, &settings);
    
    // 初始化零漂补偿算法（这是减少零漂的关键！）
    FusionOffsetInitialise(&s_offset, 416);  // 采样率 416Hz（与传感器 ODR 匹配）
    
    s_ahrs_inited = true;
    s_last_time_us = esp_timer_get_time();
    
    ESP_LOGI(TAG, "Fusion AHRS and Offset initialized with optimized settings");

    return ESP_OK;
}

/**
 * @brief 反初始化LSM6DS3传感器
 */
esp_err_t lsm6ds3_deinit(void) {
    if (!g_lsm6ds3_handle.is_initialized) {
        return ESP_OK;
    }

    // 禁用传感器
    lsm6ds3_accel_enable(false);
    lsm6ds3_gyro_enable(false);

#if LSM6DS3_USE_I2C
    if (g_lsm6ds3_handle.i2c_dev_handle) {
        i2c_master_bus_rm_device(g_lsm6ds3_handle.i2c_dev_handle);
    }
    // Do not deinit the bus, as it is shared
#else
    spi_bus_remove_device(g_lsm6ds3_handle.spi_handle);
    spi_bus_free(LSM6DS3_SPI_HOST);
#endif

    memset(&g_lsm6ds3_handle, 0, sizeof(g_lsm6ds3_handle));
    ESP_LOGI(TAG, "LSM6DS3 deinitialized");

    return ESP_OK;
}

/**
 * @brief 配置加速度计
 */
esp_err_t lsm6ds3_config_accel(uint8_t odr, uint8_t fs) {
    esp_err_t ret;
    uint8_t ctrl1_xl;

    if (!g_lsm6ds3_handle.is_initialized) {
        ESP_LOGE(TAG, "LSM6DS3 not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // 读取当前配置
    ret = lsm6ds3_read_reg(LSM6DS3_REG_CTRL1_XL, &ctrl1_xl, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    // 清除相关位
    ctrl1_xl &= ~(LSM6DS3_CTRL1_XL_ODR_XL_MASK | LSM6DS3_CTRL1_XL_FS_XL_MASK);

    // 设置新的配置
    ctrl1_xl |= (odr & LSM6DS3_CTRL1_XL_ODR_XL_MASK);
    ctrl1_xl |= (fs & LSM6DS3_CTRL1_XL_FS_XL_MASK);

    // 写入配置
    ret = lsm6ds3_write_reg(LSM6DS3_REG_CTRL1_XL, ctrl1_xl);
    if (ret == ESP_OK) {
        g_lsm6ds3_handle.accel_fs = fs;
        ESP_LOGI(TAG, "Accelerometer configured: ODR=0x%02X, FS=0x%02X", odr, fs);
    }

    return ret;
}

/**
 * @brief 配置陀螺仪
 */
esp_err_t lsm6ds3_config_gyro(uint8_t odr, uint8_t fs) {
    esp_err_t ret;
    uint8_t ctrl2_g;

    if (!g_lsm6ds3_handle.is_initialized) {
        ESP_LOGE(TAG, "LSM6DS3 not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // 读取当前配置
    ret = lsm6ds3_read_reg(LSM6DS3_REG_CTRL2_G, &ctrl2_g, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    // 清除相关位
    ctrl2_g &= ~(LSM6DS3_CTRL2_G_ODR_G_MASK | LSM6DS3_CTRL2_G_FS_G_MASK);

    // 设置新的配置
    ctrl2_g |= (odr & LSM6DS3_CTRL2_G_ODR_G_MASK);
    ctrl2_g |= (fs & LSM6DS3_CTRL2_G_FS_G_MASK);

    // 写入配置
    ret = lsm6ds3_write_reg(LSM6DS3_REG_CTRL2_G, ctrl2_g);
    if (ret == ESP_OK) {
        g_lsm6ds3_handle.gyro_fs = fs;
        ESP_LOGI(TAG, "Gyroscope configured: ODR=0x%02X, FS=0x%02X", odr, fs);
    }

    return ret;
}

/**
 * @brief 读取加速度计数据
 */
esp_err_t lsm6ds3_read_accel(lsm6ds3_accel_data_t* accel_data) {
    esp_err_t ret;
    uint8_t raw_data[6];
    int16_t raw_x, raw_y, raw_z;

    if (!g_lsm6ds3_handle.is_initialized || !accel_data) {
        return ESP_ERR_INVALID_ARG;
    }

    // 读取加速度计原始数据
    ret = lsm6ds3_read_reg(LSM6DS3_REG_OUTX_L_XL, raw_data, 6);
    if (ret != ESP_OK) {
        return ret;
    }

    // 组合16位数据
    raw_x = (int16_t)(raw_data[1] << 8 | raw_data[0]);
    raw_y = (int16_t)(raw_data[3] << 8 | raw_data[2]);
    raw_z = (int16_t)(raw_data[5] << 8 | raw_data[4]);

    // 转换为g值
    accel_data->x = lsm6ds3_convert_accel_raw_to_g(raw_x, g_lsm6ds3_handle.accel_fs);
    accel_data->y = lsm6ds3_convert_accel_raw_to_g(raw_y, g_lsm6ds3_handle.accel_fs);
    accel_data->z = lsm6ds3_convert_accel_raw_to_g(raw_z, g_lsm6ds3_handle.accel_fs);

    // 注意：校准应在应用层调用 apply_accelerometer_calibration()
    
    return ESP_OK;
}

/**
 * @brief 读取陀螺仪数据
 */
esp_err_t lsm6ds3_read_gyro(lsm6ds3_gyro_data_t* gyro_data) {
    esp_err_t ret;
    uint8_t raw_data[6];
    int16_t raw_x, raw_y, raw_z;

    if (!g_lsm6ds3_handle.is_initialized || !gyro_data) {
        return ESP_ERR_INVALID_ARG;
    }

    // 读取陀螺仪原始数据
    ret = lsm6ds3_read_reg(LSM6DS3_REG_OUTX_L_G, raw_data, 6);
    if (ret != ESP_OK) {
        return ret;
    }

    // 组合16位数据
    raw_x = (int16_t)(raw_data[1] << 8 | raw_data[0]);
    raw_y = (int16_t)(raw_data[3] << 8 | raw_data[2]);
    raw_z = (int16_t)(raw_data[5] << 8 | raw_data[4]);

    // 转换为度/秒
    gyro_data->x = lsm6ds3_convert_gyro_raw_to_dps(raw_x, g_lsm6ds3_handle.gyro_fs);
    gyro_data->y = lsm6ds3_convert_gyro_raw_to_dps(raw_y, g_lsm6ds3_handle.gyro_fs);
    gyro_data->z = lsm6ds3_convert_gyro_raw_to_dps(raw_z, g_lsm6ds3_handle.gyro_fs);

    // 注意：校准应在应用层调用 apply_gyroscope_calibration()
    
    return ESP_OK;
}

/**
 * @brief 读取温度数据
 */
esp_err_t lsm6ds3_read_temp(lsm6ds3_temp_data_t* temp_data) {
    esp_err_t ret;
    uint8_t raw_data[2];
    int16_t raw_temp;

    if (!g_lsm6ds3_handle.is_initialized || !temp_data) {
        return ESP_ERR_INVALID_ARG;
    }

    // 读取温度原始数据
    ret = lsm6ds3_read_reg(LSM6DS3_REG_OUT_TEMP_L, raw_data, 2);
    if (ret != ESP_OK) {
        return ret;
    }

    // 组合16位数据
    raw_temp = (int16_t)(raw_data[1] << 8 | raw_data[0]);

    // 转换为摄氏度
    temp_data->temperature = lsm6ds3_convert_temp_raw_to_celsius(raw_temp);

    return ESP_OK;
}

/**
 * @brief 读取所有传感器数据
 */
esp_err_t lsm6ds3_read_all(lsm6ds3_data_t* data) {
    esp_err_t ret;

    if (!g_lsm6ds3_handle.is_initialized || !data) {
        return ESP_ERR_INVALID_ARG;
    }

    // 读取加速度计数据
    ret = lsm6ds3_read_accel(&data->accel);
    if (ret != ESP_OK) {
        return ret;
    }

    // 读取陀螺仪数据
    ret = lsm6ds3_read_gyro(&data->gyro);
    if (ret != ESP_OK) {
        return ret;
    }

    // 读取温度数据
    ret = lsm6ds3_read_temp(&data->temp);
    if (ret != ESP_OK) {
        return ret;
    }

    return ESP_OK;
}

/**
 * @brief 检查传感器是否就绪
 */
bool lsm6ds3_is_ready(void) { return g_lsm6ds3_handle.is_initialized; }

/**
 * @brief 软复位传感器
 */
esp_err_t lsm6ds3_reset(void) {
    esp_err_t ret;
    uint8_t ctrl3_c;

    // 读取CTRL3_C寄存器
    ret = lsm6ds3_read_reg(LSM6DS3_REG_CTRL3_C, &ctrl3_c, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    // 设置软复位位
    ctrl3_c |= LSM6DS3_CTRL3_C_SW_RESET;

    // 写入寄存器
    ret = lsm6ds3_write_reg(LSM6DS3_REG_CTRL3_C, ctrl3_c);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Soft reset initiated");
    }

    return ret;
}

/**
 * @brief 使用 Fusion AHRS 解算并返回欧拉角 (无磁力计)
 */
esp_err_t lsm6ds3_read_euler(lsm6ds3_euler_t* euler) {
    if (!g_lsm6ds3_handle.is_initialized || !euler) {
        return ESP_ERR_INVALID_ARG;
    }

    // 若 AHRS 未初始化则初始化一次
    if (!s_ahrs_inited) {
        FusionAhrsInitialise(&s_ahrs);
        const FusionAhrsSettings settings = {
            .convention = FusionConventionNwu,
            .gain = 0.5f,
            .gyroscopeRange = 250.0f,
            .accelerationRejection = 10.0f,
            .magneticRejection = 0.0f,
            .recoveryTriggerPeriod = 5 * 416,
        };
        FusionAhrsSetSettings(&s_ahrs, &settings);
        FusionOffsetInitialise(&s_offset, 416);
        s_ahrs_inited = true;
        s_last_time_us = esp_timer_get_time();
    }

    // 读取一次传感器
    lsm6ds3_data_t data = {0};
    esp_err_t ret = lsm6ds3_read_all(&data);
    if (ret != ESP_OK) {
        return ret;
    }

    // 计算 deltaTime (s)
    const int64_t now_us = esp_timer_get_time();
    float deltaTime = (float)(now_us - s_last_time_us) / 1000000.0f;
    if (deltaTime <= 0.0f || deltaTime > 1.0f) { // 限幅，防止异常间隔
        deltaTime = 0.01f;                       // 10ms 作为回退
    }
    s_last_time_us = now_us;

    // 封装为 Fusion 向量（单位：陀螺仪 dps，加速度 g）
    FusionVector gyroscope = {.axis = {data.gyro.x, data.gyro.y, data.gyro.z}};
    const FusionVector accelerometer = {.axis = {data.accel.x, data.accel.y, data.accel.z}};

    // 【关键】应用零漂补偿算法 - 动态修正陀螺仪零漂
    gyroscope = FusionOffsetUpdate(&s_offset, gyroscope);

    // 无磁力计更新
    FusionAhrsUpdateNoMagnetometer(&s_ahrs, gyroscope, accelerometer, deltaTime);

    // 提取欧拉角（单位：度）
    const FusionEuler fe = FusionQuaternionToEuler(FusionAhrsGetQuaternion(&s_ahrs));
    euler->roll = fe.angle.roll;
    euler->pitch = fe.angle.pitch;
    euler->yaw = fe.angle.yaw; // 无磁力计时航向会缓慢漂移

    return ESP_OK;
}

/**
 * @brief 启用/禁用加速度计
 */
esp_err_t lsm6ds3_accel_enable(bool enable) {
    esp_err_t ret;
    uint8_t ctrl1_xl;

    if (!g_lsm6ds3_handle.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // 读取当前配置
    ret = lsm6ds3_read_reg(LSM6DS3_REG_CTRL1_XL, &ctrl1_xl, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    if (enable) {
        // 启用加速度计 (设置ODR)
        ctrl1_xl |= LSM6DS3_ODR_104_HZ; // 默认104Hz
    } else {
        // 禁用加速度计
        ctrl1_xl &= ~LSM6DS3_CTRL1_XL_ODR_XL_MASK;
    }

    // 写入配置
    ret = lsm6ds3_write_reg(LSM6DS3_REG_CTRL1_XL, ctrl1_xl);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Accelerometer %s", enable ? "enabled" : "disabled");
    }

    return ret;
}

/**
 * @brief 启用/禁用陀螺仪
 */
esp_err_t lsm6ds3_gyro_enable(bool enable) {
    esp_err_t ret;
    uint8_t ctrl2_g;

    if (!g_lsm6ds3_handle.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // 读取当前配置
    ret = lsm6ds3_read_reg(LSM6DS3_REG_CTRL2_G, &ctrl2_g, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    if (enable) {
        // 启用陀螺仪 (设置ODR)
        ctrl2_g |= LSM6DS3_ODR_104_HZ; // 默认104Hz
    } else {
        // 禁用陀螺仪
        ctrl2_g &= ~LSM6DS3_CTRL2_G_ODR_G_MASK;
    }

    // 写入配置
    ret = lsm6ds3_write_reg(LSM6DS3_REG_CTRL2_G, ctrl2_g);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Gyroscope %s", enable ? "enabled" : "disabled");
    }

    return ret;
}