/*
 * SX1281 minimal SPI+GPIO initialization and helpers for ESP-IDF
 *
 * Pins reference (default wiring from project README):
 *  - CS    -> IO16
 *  - MOSI  -> IO2
 *  - SCK   -> IO1
 *  - MISO  -> IO39
 *  - BUSY  -> IO15 (input)
 *  - RESET -> IO5  (output, active-low)
 *  - TXEN  -> IO45 (for external PA like AT2401C)
 *  - RXEN  -> IO48 (for external LNA like AT2401C)
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle
typedef struct sx1281_dev *sx1281_handle_t;

// Configuration for the radio wiring and SPI host
typedef struct {
    spi_host_device_t spi_host;   // SPI2_HOST or SPI3_HOST on ESP32-S3
    int pin_mosi;                 // e.g., 2
    int pin_miso;                 // e.g., 39
    int pin_sck;                  // e.g., 1
    int pin_cs;                   // e.g., 16
    int pin_busy;                 // e.g., 15
    int pin_reset;                // e.g., 5 (active-low)
    int pin_txen;                 // e.g., 45 (for external PA like AT2401C)
    int pin_rxen;                 // e.g., 48 (for external LNA like AT2401C)
    uint32_t spi_clock_hz;        // e.g., 8*1000*1000
    bool install_bus_if_needed;   // true: initialize SPI bus here if not already
} sx1281_config_t;

// Create and initialize the SX1281 device: configures GPIO, SPI device, performs HW reset.
esp_err_t sx1281_create(const sx1281_config_t *cfg, sx1281_handle_t *out_handle);

// Destroy and free the device; does NOT deinit the SPI bus. Safe to call with NULL.
void sx1281_destroy(sx1281_handle_t handle);

// Hardware reset sequence (RESET low->high) and BUSY wait.
esp_err_t sx1281_hw_reset(sx1281_handle_t handle);

// Busy wait helper with timeout_ms. Returns ESP_OK or ESP_ERR_TIMEOUT.
esp_err_t sx1281_wait_busy_low(sx1281_handle_t handle, uint32_t timeout_ms);

// Send a generic write command (opcode + payload). Waits for BUSY appropriately.
esp_err_t sx1281_write_cmd(sx1281_handle_t handle, uint8_t opcode, const uint8_t *data, size_t len);

// Send a generic read command (opcode), then reads len bytes into data. Waits for BUSY appropriately.
esp_err_t sx1281_read_cmd(sx1281_handle_t handle, uint8_t opcode, uint8_t *data, size_t len);

// Try to read status using the standard GetStatus command (opcode 0xC0). Returns 1-byte status.
esp_err_t sx1281_get_status(sx1281_handle_t handle, uint8_t *out_status);

// Convenience: build default config using project pinout described above.
static inline sx1281_config_t sx1281_default_config(void) {
    sx1281_config_t cfg = {
        .spi_host = SPI3_HOST,   // Use HSPI (SPI3) by default to avoid conflicts
        .pin_mosi = 2,
        .pin_miso = 39,
        .pin_sck  = 1,
        .pin_cs   = 16,
        .pin_busy = 15,
        .pin_reset = 5,
        .pin_txen = 45,
        .pin_rxen = 48,
        .spi_clock_hz = 8000000, // 8 MHz is safe for bring-up
        .install_bus_if_needed = true,
    };
    return cfg;
}

// Convenience: directly create with default config.
static inline esp_err_t sx1281_create_default(sx1281_handle_t *out_handle) {
    sx1281_config_t cfg = sx1281_default_config();
    return sx1281_create(&cfg, out_handle);
}

#ifdef __cplusplus
}
#endif
