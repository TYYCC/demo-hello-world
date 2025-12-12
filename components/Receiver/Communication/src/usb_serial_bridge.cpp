#include "usb_serial_bridge.h"

#include "Arduino.h"

void usb_serial_init(unsigned long baud) {
  Serial.begin(baud);
}

int usb_serial_connected(void) {
  return Serial;
}

size_t usb_serial_available(void) {
  return Serial.available();
}

size_t usb_serial_read(uint8_t *buf, size_t len) {
  return Serial.readBytes((char*)buf, len);
}

size_t usb_serial_write(const uint8_t *buf, size_t len) {
  return Serial.write(buf, len);
}

void usb_serial_flush(void) {
  Serial.flush();
}
