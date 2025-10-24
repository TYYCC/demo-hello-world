/**
 * @file st7789.h
 * @brief ST7789 TFT LCD Driver for ESP32-S3
 * @author Your Name
 * @date 2024
 */

#ifndef ST7789_H
#define ST7789_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h" // 新增头文件
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ========================================
// 硬件连接配置 (可根据实际连接修改)
// ========================================
#define ST7789_SPI_HOST         SPI2_HOST
#define ST7789_PIN_MOSI         11          // SDA/MOSI
#define ST7789_PIN_CLK          12          // SCL/CLK  
#define ST7789_PIN_CS           10          // CS 片选
#define ST7789_PIN_DC           13          // DC 数据/命令选择
#define ST7789_PIN_RST          14          // RST 复位
#define ST7789_PIN_BLK          47           // 背光控制 (可选)

// ========================================
// 显示器参数配置
// ========================================
#define ST7789_WIDTH            240         // 屏幕物理宽度
#define ST7789_HEIGHT           320         // 屏幕物理高度
#define ST7789_ROTATION         2           // 默认旋转方向 (与STM32驱动一致)
#define ST7789_RGB_ORDER        0           // RGB顺序 0=RGB, 1=BGR (根据STM32驱动设为RGB)
#define ST7789_COLOR_SWAP       1           // 颜色字节交换 0=关闭, 1=开启

// 坐标偏移量 (适配不同屏幕, 参考STM32驱动)
#if ST7789_ROTATION == 0 || ST7789_ROTATION == 2
    #define X_SHIFT                 0
    #define Y_SHIFT                 0
#else
    #define X_SHIFT                 0
    #define Y_SHIFT                 0
#endif

// ========================================
// SPI传输配置
// ========================================
#define ST7789_SPI_CLOCK_HZ     80000000    // 80MHz SPI时钟 (最大速度)
#define ST7789_SPI_QUEUE_SIZE   7           // SPI队列大小

// ========================================
// ST7789 命令定义
// ========================================
#define ST7789_CMD_NOP          0x00
#define ST7789_CMD_SWRESET      0x01
#define ST7789_CMD_RDDID        0x04
#define ST7789_CMD_RDDST        0x09

#define ST7789_CMD_SLPIN        0x10
#define ST7789_CMD_SLPOUT       0x11
#define ST7789_CMD_PTLON        0x12
#define ST7789_CMD_NORON        0x13

#define ST7789_CMD_INVOFF       0x20
#define ST7789_CMD_INVON        0x21
#define ST7789_CMD_DISPOFF      0x28
#define ST7789_CMD_DISPON       0x29
#define ST7789_CMD_CASET        0x2A
#define ST7789_CMD_RASET        0x2B
#define ST7789_CMD_RAMWR        0x2C
#define ST7789_CMD_RAMRD        0x2E

#define ST7789_CMD_PTLAR        0x30
#define ST7789_CMD_COLMOD       0x3A
#define ST7789_CMD_MADCTL       0x36

#define ST7789_MADCTL_MY        0x80  // Row Address Order
#define ST7789_MADCTL_MX        0x40  // Column Address Order
#define ST7789_MADCTL_MV        0x20  // Row/Column Exchange
#define ST7789_MADCTL_ML        0x10  // Line Address Order
#define ST7789_MADCTL_RGB       0x00  // RGB Order (0=RGB, 0x08=BGR)

// Commands from working STM32 driver, required for full init
#define ST7789_CMD_PORCTRL      0xB2  // Porch Control
#define ST7789_CMD_GCTRL        0xB7  // Gate Control
#define ST7789_CMD_VCOMS        0xBB  // VCOM Setting
#define ST7789_CMD_LCMCTRL      0xC2  // LCM Control
#define ST7789_CMD_VDVVRHEN     0xC3  // VDV and VRH Command Enable
#define ST7789_CMD_VRHSET       0xC4  // VRH Set
#define ST7789_CMD_VDVSET       0xC6  // VDV Set
#define ST7789_CMD_PWCTRL1      0xD0  // Power Control 1
#define ST7789_CMD_GMCTRP1      0xE0  // Positive Gamma Correction
#define ST7789_CMD_GMCTRN1      0xE1  // Negative Gamma Correction

// ========================================
// 颜色定义 (RGB565格式)
// ========================================
#define ST7789_BLACK            0x0000
#define ST7789_WHITE            0xFFFF
#define ST7789_RED              0xF800
#define ST7789_GREEN            0x07E0
#define ST7789_BLUE             0x001F
#define ST7789_YELLOW           0xFFE0
#define ST7789_CYAN             0x07FF
#define ST7789_MAGENTA          0xF81F

// ========================================
// 数据结构定义
// ========================================
typedef struct {
    spi_device_handle_t spi_handle;
    bool is_initialized;
    uint16_t width;
    uint16_t height;
    uint8_t rotation;
} st7789_handle_t;

// ========================================
// 函数声明
// ========================================

/**
 * @brief 初始化ST7789显示器
 * @return ESP_OK 成功, 其他值表示错误
 */
esp_err_t st7789_init(void);

/**
 * @brief 反初始化ST7789显示器
 * @return ESP_OK 成功
 */
esp_err_t st7789_deinit(void);

/**
 * @brief 设置显示窗口
 * @param x0 起始X坐标
 * @param y0 起始Y坐标  
 * @param x1 结束X坐标
 * @param y1 结束Y坐标
 */
void st7789_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

/**
 * @brief 写入像素数据
 * @param data 像素数据缓冲区
 * @param length 数据长度(像素数量)
 */
void st7789_write_pixels(const uint16_t *data, size_t length);
void st7789_fill_area(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);

/**
 * @brief 清空整个屏幕
 * @param color RGB565颜色值
 */
void st7789_clear_screen(uint16_t color);

/**
 * @brief 设置屏幕旋转
 * @param rotation 旋转值 0/1/2/3 对应 0°/90°/180°/270°
 */
void st7789_set_rotation(uint8_t rotation);

/**
 * @brief 控制显示开关
 * @param enable true=开启显示, false=关闭显示
 */
void st7789_display_enable(bool enable);

/**
 * @brief 控制背光亮度
 * @param brightness 亮度值 (0-100)
 */
void st7789_set_backlight(uint8_t brightness);

/**
 * @brief 获取显示器句柄
 * @return 显示器句柄指针
 */
st7789_handle_t* st7789_get_handle(void);

#ifdef __cplusplus
}
#endif

#endif /* ST7789_H */
