/**
 * @file st7789.c
 * @brief ST7789 TFT LCD Driver Implementation for ESP32-S3
 * @author Your Name
 * @date 2024
 */

#include "st7789.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include <string.h>

// ========================================
// 私有变量和常量
// ========================================
static const char *TAG = "ST7789";
static st7789_handle_t g_st7789_handle = {0};

// 定义PWM参数 - st7789_set_backlight需要这些
#define BLK_PWM_TIMER LEDC_TIMER_0
#define BLK_PWM_CHANNEL LEDC_CHANNEL_0
#define BLK_PWM_RESOLUTION LEDC_TIMER_10_BIT

// ========================================
// 私有函数声明
// ========================================
static esp_err_t st7789_spi_init(void);
static esp_err_t st7789_gpio_init(void);
static void st7789_write_cmd(uint8_t cmd);
static void st7789_write_data(uint8_t data);
static void st7789_write_data_buf(const uint8_t *data, size_t length);
static void st7789_hardware_reset(void);
static void st7789_init_sequence(void);
static void st7789_write_bytes_async(const uint8_t *data, size_t size);
static void st7789_backlight_pwm_init(void);

// ========================================
// SPI传输相关函数
// ========================================

/**
 * @brief 初始化SPI接口
 */
static esp_err_t st7789_spi_init(void)
{
    esp_err_t ret;
    
    // SPI总线配置
    spi_bus_config_t bus_config = {
        .mosi_io_num = ST7789_PIN_MOSI,
        .miso_io_num = 16,                      // 添加MISO支持，供XPT2046使用
        .sclk_io_num = ST7789_PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = ST7789_WIDTH * ST7789_HEIGHT * 2,  // 整屏大小
    };
    
    // 初始化SPI总线
    ret = spi_bus_initialize(ST7789_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus initialize failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // SPI设备配置
    spi_device_interface_config_t dev_config = {
        .clock_speed_hz = ST7789_SPI_CLOCK_HZ,
        .mode = 0,                              // SPI模式0
        .spics_io_num = ST7789_PIN_CS,
        .queue_size = ST7789_SPI_QUEUE_SIZE,   // 允许排队多个事务
        .pre_cb = NULL,
        .post_cb = NULL,
    };
    
    // 添加SPI设备
    ret = spi_bus_add_device(ST7789_SPI_HOST, &dev_config, &g_st7789_handle.spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(ret));
        spi_bus_free(ST7789_SPI_HOST);
        return ret;
    }
    
    ESP_LOGI(TAG, "SPI initialized successfully");
    return ESP_OK;
}

/**
 * @brief 初始化GPIO
 */
static esp_err_t st7789_gpio_init(void)
{
    // DC引脚配置
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << ST7789_PIN_DC),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    gpio_config(&io_conf);
    
    // RST引脚配置
    io_conf.pin_bit_mask = (1ULL << ST7789_PIN_RST);
    gpio_config(&io_conf);
    
    // 背光引脚配置
    io_conf.pin_bit_mask = (1ULL << ST7789_PIN_BLK);
    gpio_config(&io_conf);
    
    // 设置初始状态
    gpio_set_level(ST7789_PIN_DC, 0);
    gpio_set_level(ST7789_PIN_RST, 1);
    gpio_set_level(ST7789_PIN_BLK, 0);
    
    ESP_LOGI(TAG, "GPIO initialized successfully");
    return ESP_OK;
}

/**
 * @brief 初始化背光PWM
 */
static void st7789_backlight_pwm_init(void)
{
    // 配置LEDC定时器
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = BLK_PWM_RESOLUTION,
        .timer_num = BLK_PWM_TIMER,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    // 配置LEDC通道
    ledc_channel_config_t ledc_channel = {
        .gpio_num = ST7789_PIN_BLK,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BLK_PWM_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BLK_PWM_TIMER,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&ledc_channel);
    ESP_LOGI(TAG, "Backlight PWM initialized");
}

/**
 * @brief 写入命令
 */
static void st7789_write_cmd(uint8_t cmd)
{
    esp_err_t ret;
    spi_transaction_t trans = {0};
    
    trans.length = 8;                       // 8位数据
    trans.tx_buffer = &cmd;
    
    gpio_set_level(ST7789_PIN_DC, 0);       // DC=0表示命令
    ret = spi_device_polling_transmit(g_st7789_handle.spi_handle, &trans);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI command write failed");
    }
}

/**
 * @brief 写入单字节数据
 */
static void st7789_write_data(uint8_t data)
{
    esp_err_t ret;
    spi_transaction_t trans = {0};
    
    trans.length = 8;                       // 8位数据
    trans.tx_buffer = &data;
    
    gpio_set_level(ST7789_PIN_DC, 1);       // DC=1表示数据
    ret = spi_device_polling_transmit(g_st7789_handle.spi_handle, &trans);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI data write failed");
    }
}

/**
 * @brief 写入数据缓冲区
 */
static void st7789_write_data_buf(const uint8_t *data, size_t length)
{
    if (length == 0) return;
    
    esp_err_t ret;
    spi_transaction_t trans = {0};
    
    trans.length = length * 8;              // 位数
    trans.tx_buffer = data;
    
    gpio_set_level(ST7789_PIN_DC, 1);       // DC=1表示数据
    ret = spi_device_polling_transmit(g_st7789_handle.spi_handle, &trans);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI data buffer write failed");
    }
}

// ==========================
// 异步DMA流水线发送实现
// ==========================
#define ST7789_DMA_CHUNK_BYTES 8192   // 8KB 分块
#define ST7789_DMA_QUEUE_DEPTH 4      // 队列深度4（四缓冲流水线）
static uint8_t *s_dma_buf[ST7789_DMA_QUEUE_DEPTH] = {0};

static void st7789_write_bytes_async(const uint8_t *data, size_t size)
{
    if (size == 0) return;

    // 确保DMA缓冲已分配
    for (int i = 0; i < ST7789_DMA_QUEUE_DEPTH; i++) {
        if (!s_dma_buf[i]) s_dma_buf[i] = (uint8_t*)heap_caps_malloc(ST7789_DMA_CHUNK_BYTES, MALLOC_CAP_DMA);
    }

    // 如果无法分配DMA缓冲，退化为阻塞发送
    bool dma_ok = true;
    for (int i = 0; i < ST7789_DMA_QUEUE_DEPTH; i++) if (!s_dma_buf[i]) dma_ok = false;
    if (!dma_ok) {
        st7789_write_data_buf(data, size);
        return;
    }

    // 置DC为数据模式
    gpio_set_level(ST7789_PIN_DC, 1);

    spi_transaction_t trans[ST7789_DMA_QUEUE_DEPTH];
    memset(trans, 0, sizeof(trans));
    bool in_use[ST7789_DMA_QUEUE_DEPTH] = {0};

    int queued = 0; // in-flight count
    const uint8_t *src = data;
    size_t bytes_left = size;

    while (bytes_left > 0) {
        // 若队列已满，取回一个事务并标记对应slot空闲
        if (queued == ST7789_DMA_QUEUE_DEPTH) {
            spi_transaction_t *ret_trans;
            spi_device_get_trans_result(g_st7789_handle.spi_handle, &ret_trans, portMAX_DELAY);
            int freed = (int)(uintptr_t)ret_trans->user;
            if (freed >= 0 && freed < ST7789_DMA_QUEUE_DEPTH) in_use[freed] = false;
            queued--;
        }

        // 找到一个空闲slot
        int slot = -1;
        for (int i = 0; i < ST7789_DMA_QUEUE_DEPTH; i++) {
            if (!in_use[i]) { slot = i; break; }
        }
        if (slot < 0) {
            // 理论上不会发生，保险等待一个完成
            spi_transaction_t *ret_trans;
            spi_device_get_trans_result(g_st7789_handle.spi_handle, &ret_trans, portMAX_DELAY);
            int freed = (int)(uintptr_t)ret_trans->user;
            if (freed >= 0 && freed < ST7789_DMA_QUEUE_DEPTH) in_use[freed] = false;
            queued--;
            continue;
        }

        // 准备一个事务
        size_t chunk = bytes_left > ST7789_DMA_CHUNK_BYTES ? ST7789_DMA_CHUNK_BYTES : bytes_left;
        uint8_t *buf = s_dma_buf[slot];

        // 如果源缓冲区是DMA可达，直接引用；否则复制到DMA缓冲
        const uint8_t *tx_ptr = NULL;
        bool src_dma_ok = esp_ptr_dma_capable(src);
        if (src_dma_ok) {
            tx_ptr = src;
        } else {
            memcpy(buf, src, chunk);
            tx_ptr = buf;
        }

        spi_transaction_t *t = &trans[slot];
        memset(t, 0, sizeof(*t));
        t->length = chunk * 8;
        t->tx_buffer = tx_ptr;
        t->user = (void*)(uintptr_t)slot;

        esp_err_t ret = spi_device_queue_trans(g_st7789_handle.spi_handle, t, portMAX_DELAY);
        if (ret != ESP_OK) {
            // 退化为阻塞发送
            st7789_write_data_buf(src, chunk);
        } else {
            in_use[slot] = true;
            queued++;
        }

        src += chunk;
        bytes_left -= chunk;
    }

    // 取回所有剩余事务
    while (queued > 0) {
        spi_transaction_t *ret_trans;
        spi_device_get_trans_result(g_st7789_handle.spi_handle, &ret_trans, portMAX_DELAY);
        int freed = (int)(uintptr_t)ret_trans->user;
        if (freed >= 0 && freed < ST7789_DMA_QUEUE_DEPTH) in_use[freed] = false;
        queued--;
    }
}

/**
 * @brief 硬件复位
 */
static void st7789_hardware_reset(void)
{
    gpio_set_level(ST7789_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(100));         // 延时100ms
    gpio_set_level(ST7789_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(100));         // 延时100ms
    
    ESP_LOGI(TAG, "Hardware reset completed");
}

/**
 * @brief ST7789初始化序列
 */
static void st7789_init_sequence(void)
{
    ESP_LOGI(TAG, "Starting ST7789 initialization sequence based on manufacturer driver");

    // The reference driver does not use software reset, only hardware reset.
    // Hardware reset is handled in st7789_init() before this function is called.
    // st7789_write_cmd(ST7789_CMD_SWRESET);
    // vTaskDelay(pdMS_TO_TICKS(150));

    // 退出睡眠模式
    st7789_write_cmd(ST7789_CMD_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(120)); // Reference driver uses 100ms, 120ms is fine.

    // 内存访问控制 (旋转和颜色顺序)
    st7789_set_rotation(ST7789_ROTATION);
    
    // 设置颜色模式 (Reference: 0x05)
    st7789_write_cmd(ST7789_CMD_COLMOD);
    st7789_write_data(0x05); 

    // Frame rate setting (from reference)
    st7789_write_cmd(ST7789_CMD_PORCTRL); // 0xB2
    st7789_write_data_buf((const uint8_t[]){0x0C, 0x0C, 0x00, 0x33, 0x33}, 5);

    // Gate Control (from reference)
    st7789_write_cmd(ST7789_CMD_GCTRL); // 0xB7
    st7789_write_data(0x35);

    // VCOM Setting (Reference: 0x35)
    st7789_write_cmd(ST7789_CMD_VCOMS); // 0xBB
    st7789_write_data(0x35);

    // Power setting (from reference) - some commands are not in standard datasheet but in ref driver
    st7789_write_cmd(0xC0); 
    st7789_write_data(0x2C);
    
    st7789_write_cmd(ST7789_CMD_LCMCTRL); // 0xC2
    st7789_write_data(0x01);

    st7789_write_cmd(ST7789_CMD_VDVVRHEN); // 0xC3, (Reference uses: 0x13)
    st7789_write_data(0x13);

    st7789_write_cmd(ST7789_CMD_VRHSET); // 0xC4, (Reference uses: 0x20)
    st7789_write_data(0x20);

    st7789_write_cmd(ST7789_CMD_VDVSET); // 0xC6, (Reference uses: 0x0F)
    st7789_write_data(0x0F);

    st7789_write_cmd(0xCA); // Unknown command from reference
    st7789_write_data(0x0F);

    st7789_write_cmd(0xC8); // Unknown command from reference
    st7789_write_data(0x08);

    st7789_write_cmd(0x55); // WRCTRLD / CABC Control? from reference
    st7789_write_data(0x90);

    st7789_write_cmd(ST7789_CMD_PWCTRL1); // 0xD0
    st7789_write_data_buf((const uint8_t[]){0xA4, 0xA1}, 2);

    // Positive Gamma Correction (from manufacturer)
    st7789_write_cmd(ST7789_CMD_GMCTRP1); // 0xE0
    st7789_write_data_buf((const uint8_t[]){0xD0, 0x08, 0x0A, 0x0D, 0x0B, 0x07, 0x21, 0x33, 0x39, 0x39, 0x16, 0x16, 0x1F, 0x3C}, 14);

    // Negative Gamma Correction (from manufacturer)
    st7789_write_cmd(ST7789_CMD_GMCTRN1); // 0xE1
    st7789_write_data_buf((const uint8_t[]){0xD0, 0x00, 0x03, 0x01, 0x00, 0x10, 0x21, 0x32, 0x38, 0x16, 0x14, 0x14, 0x20, 0x3D}, 14);

    // The reference driver does NOT turn on color inversion. This is a likely cause of issues.
    st7789_write_cmd(ST7789_CMD_INVON);
    
    // The reference driver does not use NORON, it just calls DISPON.
    // st7789_write_cmd(ST7789_CMD_NORON);
    
    // 开启显示
    st7789_write_cmd(ST7789_CMD_DISPON);
    vTaskDelay(pdMS_TO_TICKS(10)); // A small delay after display on is good practice.

    ESP_LOGI(TAG, "ST7789 initialization sequence updated based on manufacturer driver");
}

// ========================================
// 公共函数实现
// ========================================

/**
 * @brief 初始化ST7789
 */
esp_err_t st7789_init(void)
{
    esp_err_t ret;
    
    ESP_LOGI(TAG, "Initializing ST7789 display driver");
    
    // 初始化句柄
    memset(&g_st7789_handle, 0, sizeof(st7789_handle_t));
    g_st7789_handle.width = ST7789_WIDTH;
    g_st7789_handle.height = ST7789_HEIGHT;
    g_st7789_handle.rotation = ST7789_ROTATION;
    
    // 初始化GPIO
    ret = st7789_gpio_init();
    if (ret != ESP_OK) {
        return ret;
    }
    
    // 初始化SPI
    ret = st7789_spi_init();
    if (ret != ESP_OK) {
        return ret;
    }
    
    // 初始化背光PWM
    st7789_backlight_pwm_init();
    
    // 硬件复位
    st7789_hardware_reset();
    
    // 初始化序列
    st7789_init_sequence();
    
    // 在components_init中加载NVS设置后统一设置背光
    // st7789_set_backlight(100); 
    
    // 清屏为黑色
    st7789_clear_screen(ST7789_BLACK);
    
    g_st7789_handle.is_initialized = true;
    ESP_LOGI(TAG, "ST7789 initialized successfully");
    
    return ESP_OK;
}

/**
 * @brief 反初始化ST7789
 */
esp_err_t st7789_deinit(void)
{
    if (!g_st7789_handle.is_initialized) {
        return ESP_OK;
    }
    
    // 关闭显示
    st7789_display_enable(false);
    st7789_set_backlight(0);
    
    // 释放SPI设备
    spi_bus_remove_device(g_st7789_handle.spi_handle);
    spi_bus_free(ST7789_SPI_HOST);
    
    g_st7789_handle.is_initialized = false;
    ESP_LOGI(TAG, "ST7789 deinitialized");
    
    return ESP_OK;
}

/**
 * @brief 设置显示窗口
 */
void st7789_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    // 应用坐标偏移
    x0 += X_SHIFT;
    x1 += X_SHIFT;
    y0 += Y_SHIFT;
    y1 += Y_SHIFT;

    // 设置列地址
    st7789_write_cmd(ST7789_CMD_CASET);
    st7789_write_data(x0 >> 8);
    st7789_write_data(x0 & 0xFF);
    st7789_write_data(x1 >> 8);
    st7789_write_data(x1 & 0xFF);
    
    // 设置行地址
    st7789_write_cmd(ST7789_CMD_RASET);
    st7789_write_data(y0 >> 8);
    st7789_write_data(y0 & 0xFF);
    st7789_write_data(y1 >> 8);
    st7789_write_data(y1 & 0xFF);
    
    // 准备写入RAM
    st7789_write_cmd(ST7789_CMD_RAMWR);
}

/**
 * @brief 写入像素数据
 */
void st7789_write_pixels(const uint16_t *data, size_t length)
{
    if (data == NULL || length == 0) {
        return;
    }
    // 零转换路径 + 异步DMA流水线
    st7789_write_bytes_async((const uint8_t *)data, length * 2);
}

/**
 * @brief 填充颜色到指定区域
 */
void st7789_fill_area(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    if (x0 >= ST7789_WIDTH || y0 >= ST7789_HEIGHT) {
        return;
    }
    
    if (x1 >= ST7789_WIDTH) x1 = ST7789_WIDTH - 1;
    if (y1 >= ST7789_HEIGHT) y1 = ST7789_HEIGHT - 1;
    
    // 设置窗口
    st7789_set_window(x0, y0, x1, y1);
    
    // 计算像素数量
    uint32_t pixel_count = (x1 - x0 + 1) * (y1 - y0 + 1);
    
    // 创建颜色缓冲区 - 减小批次大小，避免传输问题
    uint16_t color_buffer[32];  // 32像素的缓冲区
    for (int i = 0; i < 32; i++) {
        color_buffer[i] = color;
    }
    
    // 分批发送数据
    while (pixel_count > 0) {
        uint32_t chunk_size = (pixel_count > 32) ? 32 : pixel_count;
        st7789_write_pixels(color_buffer, chunk_size);
        pixel_count -= chunk_size;
    }
}

/**
 * @brief 清空屏幕
 */
void st7789_clear_screen(uint16_t color)
{
    st7789_fill_area(0, 0, ST7789_WIDTH - 1, ST7789_HEIGHT - 1, color);
}

/**
 * @brief 设置旋转
 */
void st7789_set_rotation(uint8_t rotation)
{
    uint8_t madctl_value = 0;
    
    rotation %= 4; // 标准化旋转值

    // 根据参考STM32代码设置旋转，并始终包含RGB标志
    switch (rotation) {
        case 0:
            madctl_value = ST7789_MADCTL_MX | ST7789_MADCTL_MY | ST7789_MADCTL_RGB;
            g_st7789_handle.width = ST7789_WIDTH;
            g_st7789_handle.height = ST7789_HEIGHT;
            break;
        case 1:
            madctl_value = ST7789_MADCTL_MY | ST7789_MADCTL_MV | ST7789_MADCTL_RGB;
            g_st7789_handle.width = ST7789_HEIGHT;
            g_st7789_handle.height = ST7789_WIDTH;
            break;
        case 2:
            madctl_value = ST7789_MADCTL_RGB;  // 只设置RGB标志，与参考代码一致
            g_st7789_handle.width = ST7789_WIDTH;
            g_st7789_handle.height = ST7789_HEIGHT;
            break;
        case 3:
            madctl_value = ST7789_MADCTL_MX | ST7789_MADCTL_MV | ST7789_MADCTL_RGB;
            g_st7789_handle.width = ST7789_HEIGHT;
            g_st7789_handle.height = ST7789_WIDTH;
            break;
    }
    
    st7789_write_cmd(ST7789_CMD_MADCTL);
    st7789_write_data(madctl_value);
    
    g_st7789_handle.rotation = rotation;
    ESP_LOGI(TAG, "Rotation set to %d, MADCTL=0x%02X", rotation, madctl_value);
}

/**
 * @brief 控制显示
 */
void st7789_display_enable(bool enable)
{
    if (enable) {
        st7789_write_cmd(ST7789_CMD_DISPON);
    } else {
        st7789_write_cmd(ST7789_CMD_DISPOFF);
    }
    ESP_LOGI(TAG, "Display %s", enable ? "enabled" : "disabled");
}

/**
 * @brief 控制背光亮度
 */
void st7789_set_backlight(uint8_t brightness)
{
    if (brightness > 100) {
        brightness = 100;
    }
    else if (brightness < 20) {
        brightness = 20;
    }

    // 计算占空比
    uint32_t duty = (1 << BLK_PWM_RESOLUTION) * brightness / 100;
    if (brightness == 100) {
        duty = (1 << BLK_PWM_RESOLUTION) -1;
    }


    ledc_set_duty(LEDC_LOW_SPEED_MODE, BLK_PWM_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BLK_PWM_CHANNEL);
    ESP_LOGD(TAG, "Backlight set to %d%% (duty: %lu)", brightness, duty);
}

/**
 * @brief 获取句柄
 */
st7789_handle_t* st7789_get_handle(void)
{
    return &g_st7789_handle;
}
