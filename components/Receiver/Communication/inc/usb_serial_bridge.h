#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Arduino USBSerial bridge for C code
void usb_serial_init(unsigned long baud);
int usb_serial_connected(void);
size_t usb_serial_available(void);
size_t usb_serial_read(uint8_t *buf, size_t len);
size_t usb_serial_write(const uint8_t *buf, size_t len);
void usb_serial_flush(void);

#ifdef __cplusplus
}
#endif
