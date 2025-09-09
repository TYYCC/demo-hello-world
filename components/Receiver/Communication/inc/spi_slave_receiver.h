#ifndef SPI_SLAVE_RECEIVER_H
#define SPI_SLAVE_RECEIVER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// SPI 从机所用主机与引脚（ESP32-S3 可矩阵映射，尽量避开显示屏 SPI2）
#define SPI_RX_HOST SPI3_HOST
#define SPI_SLAVE_PIN_MOSI 40
#define SPI_SLAVE_PIN_MISO 41
#define SPI_SLAVE_PIN_SCLK 42
#define SPI_SLAVE_PIN_CS 39

// 事务与缓冲配置
#define SPI_RX_QUEUE_SIZE 2
#define SPI_RX_TRANSACTION_SZ 512 // 单次事务最大接收字节数
#define SPI_RX_BUFFER_SZ 1024     // 累积解析缓冲

esp_err_t spi_receiver_init(void);
void spi_receiver_start(void);
void spi_receiver_stop(void);

#ifdef __cplusplus
}
#endif

#endif // SPI_SLAVE_RECEIVER_H
