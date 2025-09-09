#ifndef USB_DEVICE_RECEIVER_H
#define USB_DEVICE_RECEIVER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ESP32-S3 原生 USB 引脚（固定 IO20: D+, IO19: D-）
#define USB_DP_PIN 20
#define USB_DM_PIN 19

// CDC 接收缓冲
#define USB_RX_CHUNK_SIZE 256
#define USB_RX_BUFFER_SIZE 4096

esp_err_t usb_receiver_init(void);
void usb_receiver_start(void);
void usb_receiver_stop(void);

#ifdef __cplusplus
}
#endif

#endif // USB_DEVICE_RECEIVER_H
