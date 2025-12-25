#ifndef LOGGING_USB_H
#define LOGGING_USB_H

#include <Stream.h>

// Simple wrapper stream that writes to stdout (USB-Serial-JTAG)
class USBStream : public Stream {
public:
    // Write a single character
    size_t write(uint8_t c) override;

    // Write multiple bytes
    size_t write(const uint8_t *buffer, size_t size) override;

    // Read is not supported
    int read() override;
    int available() override;
    void flush() override;
};

// Global USB stream instance
extern USBStream usbStream;

#endif
