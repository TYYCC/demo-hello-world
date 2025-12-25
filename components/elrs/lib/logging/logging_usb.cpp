#include <cstdio>
#include <Stream.h>

// Simple wrapper stream that writes to stdout (USB-Serial-JTAG)
class USBStream : public Stream {
public:
    // Write a single character
    size_t write(uint8_t c) override {
        putchar(c);
        return 1;
    }

    // Write multiple bytes
    size_t write(const uint8_t *buffer, size_t size) override {
        for (size_t i = 0; i < size; i++) {
            putchar(buffer[i]);
        }
        return size;
    }

    // Read is not supported
    int read() override {
        return -1;
    }

    int available() override {
        return 0;
    }

    void flush() override {
        fflush(stdout);
    }
};

// Global USB stream instance
USBStream usbStream;
