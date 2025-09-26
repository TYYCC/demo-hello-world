#include "gt911.h"
#include "bsp_i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "GT911";

static i2c_master_dev_handle_t g_gt911_dev_handle = NULL;
static volatile bool g_touch_irq_flag = false;

// GT911 寄存器地址 (基于真实数据手册)
#define GT911_REG_COMMAND       0x8040
#define GT911_REG_CONFIG_DATA   0x8047
#define GT911_REG_CONFIG_CHKSUM 0x80FF
#define GT911_REG_CONFIG_FRESH  0x8100
#define GT911_REG_PRODUCT_ID    0x8140
#define GT911_REG_FIRMWARE_VER  0x8144
#define GT911_REG_X_RESOLUTION  0x8146
#define GT911_REG_Y_RESOLUTION  0x8148
#define GT911_REG_VENDOR_ID     0x814A
#define GT911_REG_TOUCH_STATUS  0x814E
#define GT911_REG_POINT1        0x814F
#define GT911_REG_POINT2        0x8157
#define GT911_REG_POINT3        0x815F
#define GT911_REG_POINT4        0x8167
#define GT911_REG_POINT5        0x816F

// GT911 命令
#define GT911_CMD_READ_STATUS   0x00
#define GT911_CMD_CLEAR_STATUS  0x00

static void IRAM_ATTR gt911_isr_handler(void* arg) { g_touch_irq_flag = true; }

static esp_err_t gt911_read_reg(uint16_t reg, uint8_t* data, size_t len) {
    uint8_t reg_buf[2] = {(reg >> 8) & 0xFF, reg & 0xFF};
    return i2c_master_transmit_receive(g_gt911_dev_handle, reg_buf, 2, data, len, pdMS_TO_TICKS(100));
}

static esp_err_t gt911_write_reg(uint16_t reg, uint8_t data) {
    uint8_t write_buf[3] = {(reg >> 8) & 0xFF, reg & 0xFF, data};
    return i2c_master_transmit(g_gt911_dev_handle, write_buf, sizeof(write_buf), pdMS_TO_TICKS(100));
}

esp_err_t gt911_init(void) {
    esp_err_t ret;

    i2c_master_bus_handle_t bus_handle = bsp_i2c_get_bus_handle();
    if (bus_handle == NULL) {
        ESP_LOGE(TAG, "I2C bus not initialized");
        return ESP_FAIL;
    }

    // 配置复位引脚 (如果定义了)
#ifdef GT911_RST_PIN
    gpio_config_t rst_conf = {
        .pin_bit_mask = (1ULL << GT911_RST_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&rst_conf);
    
    // 执行硬件复位序列
    gpio_set_level(GT911_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(GT911_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
#endif

    // 配置中断引脚
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GT911_INT_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&io_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(GT911_INT_PIN, gt911_isr_handler, NULL);

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = GT911_I2C_ADDR,
        .scl_speed_hz = BSP_I2C_FREQ_HZ,
    };
    ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &g_gt911_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add GT911 device: %s", esp_err_to_name(ret));
        return ret;
    }

    // 等待GT911启动完成
    vTaskDelay(pdMS_TO_TICKS(50));

    // 读取产品ID验证通信
    uint8_t product_id[4];
    ret = gt911_read_reg(GT911_REG_PRODUCT_ID, product_id, 4);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read product ID: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "GT911 Product ID: %c%c%c%c", product_id[0], product_id[1], product_id[2], product_id[3]);

    // 读取固件版本
    uint8_t firmware_ver[2];
    ret = gt911_read_reg(GT911_REG_FIRMWARE_VER, firmware_ver, 2);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "GT911 Firmware version: 0x%02X%02X", firmware_ver[1], firmware_ver[0]);
    }

    // 读取分辨率
    uint8_t resolution[4];
    ret = gt911_read_reg(GT911_REG_X_RESOLUTION, resolution, 4);
    if (ret == ESP_OK) {
        uint16_t x_res = (resolution[1] << 8) | resolution[0];
        uint16_t y_res = (resolution[3] << 8) | resolution[2];
        ESP_LOGI(TAG, "GT911 Resolution: %dx%d", x_res, y_res);
    }

    // 清空触摸状态
    gt911_write_reg(GT911_REG_TOUCH_STATUS, 0x00);

    ESP_LOGI(TAG, "GT911 initialized successfully");
    return ESP_OK;
}

uint8_t gt911_get_touch_points(void) {
    uint8_t touch_status = 0;
    esp_err_t ret = gt911_read_reg(GT911_REG_TOUCH_STATUS, &touch_status, 1);
    if (ret == ESP_OK) {
        // GT911 触摸状态格式: bit[7] 触摸有效标志, bit[3:0] 触摸点数
        if (touch_status & 0x80) {  // 检查触摸有效标志
            return touch_status & 0x0F;  // 返回触摸点数
        }
    }
    return 0;
}

esp_err_t gt911_read_touch_points(gt911_touch_point_t* points, uint8_t* num_points) {
    if (points == NULL || num_points == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *num_points = gt911_get_touch_points();
    if (*num_points > 0 && *num_points <= 5) {  // GT911最多支持5点触摸
        // 读取第一个触摸点数据 (每个点8字节)
        uint8_t point_data[8];
        esp_err_t ret = gt911_read_reg(GT911_REG_POINT1, point_data, 8);
        if (ret == ESP_OK) {
            // GT911触摸点数据格式:
            // Byte 0: Track ID (bit[3:0])
            // Byte 1-2: X坐标 (小端格式)
            // Byte 3-4: Y坐标 (小端格式)  
            // Byte 5-6: 触摸面积 (小端格式)
            // Byte 7: 保留
            
            points[0].touch_id = point_data[0] & 0x0F;
            points[0].x = point_data[1] | (point_data[2] << 8);
            points[0].y = point_data[3] | (point_data[4] << 8);
            points[0].area = point_data[5] | (point_data[6] << 8);
            points[0].weight = 0;  // GT911没有weight数据
            
            // 如果需要读取更多触摸点，可以继续读取其他点的寄存器
            // 这里只实现了单点触摸
            if (*num_points > 1) {
                *num_points = 1;  // 暂时只支持单点
            }
            
            // 清空触摸状态寄存器，告诉GT911数据已被读取
            gt911_write_reg(GT911_REG_TOUCH_STATUS, 0x00);
            return ESP_OK;
        }
        return ret;
    }
    
    // 没有触摸或触摸点数无效时，清空状态
    if (*num_points == 0) {
        gt911_write_reg(GT911_REG_TOUCH_STATUS, 0x00);
    }
    
    return ESP_OK;
}
