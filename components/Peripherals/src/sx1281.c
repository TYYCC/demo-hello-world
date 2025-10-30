/*
 * Minimal SX1281 driver: SPI wiring + basic reset/busy + status read
 * This doesn't configure radio parameters; it's focused on correct pin init per project pinout.
 */

#include <string.h>
#include "sx1281.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"

#define TAG "SX1281"

// Known command opcodes (subset). Note: Semtech docs define GetStatus = 0xC0.
#define SX1281_CMD_GET_STATUS 0xC0

struct sx1281_dev {
    sx1281_config_t cfg;
    spi_device_handle_t spi;
};

static esp_err_t sx1281_spi_add_device(sx1281_handle_t dev) {
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = (int)dev->cfg.spi_clock_hz,
        .mode = 0,                 // SX128x uses SPI mode 0
        .spics_io_num = dev->cfg.pin_cs,
        .queue_size = 3,
        .pre_cb = NULL,
        .post_cb = NULL,
        .flags = 0,
        .duty_cycle_pos = 128,
        .cs_ena_pretrans = 2,      // small lead time for CS
        .cs_ena_posttrans = 2,
    };
    return spi_bus_add_device(dev->cfg.spi_host, &devcfg, &dev->spi);
}

static esp_err_t sx1281_spi_init_bus_if_needed(const sx1281_config_t *cfg) {
    if (!cfg->install_bus_if_needed) {
        // Assume the bus was already initialized by application
        return ESP_OK;
    }
    spi_bus_config_t buscfg = {
        .mosi_io_num = cfg->pin_mosi,
        .miso_io_num = cfg->pin_miso,
        .sclk_io_num = cfg->pin_sck,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0,
        .flags = SPICOMMON_BUSFLAG_MASTER,
        .intr_flags = 0,
    };
    // It's okay if bus already initialized; return ESP_ERR_INVALID_STATE in that case.
    esp_err_t err = spi_bus_initialize(cfg->spi_host, &buscfg, SPI_DMA_CH_AUTO);
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "SPI bus %d already initialized, continue", cfg->spi_host);
        return ESP_OK;
    }
    return err;
}

static void sx1281_gpio_init(const sx1281_config_t *cfg) {
    // RESET as output, default high (inactive)
    if (cfg->pin_reset >= 0) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << cfg->pin_reset,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io);
        gpio_set_level(cfg->pin_reset, 1);
    }

    // BUSY as input
    if (cfg->pin_busy >= 0) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << cfg->pin_busy,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io);
    }

    // TXEN/RXEN as outputs default low
    if (cfg->pin_txen >= 0) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << cfg->pin_txen,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io);
        gpio_set_level(cfg->pin_txen, 0);
    }
    if (cfg->pin_rxen >= 0) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << cfg->pin_rxen,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io);
        gpio_set_level(cfg->pin_rxen, 0);
    }
}

esp_err_t sx1281_wait_busy_low(sx1281_handle_t handle, uint32_t timeout_ms) {
    if (!handle || handle->cfg.pin_busy < 0) return ESP_ERR_INVALID_ARG;
    TickType_t start = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    while (gpio_get_level(handle->cfg.pin_busy)) {
        if ((xTaskGetTickCount() - start) >= timeout_ticks) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return ESP_OK;
}

esp_err_t sx1281_hw_reset(sx1281_handle_t handle) {
    if (!handle || handle->cfg.pin_reset < 0) return ESP_ERR_INVALID_ARG;
    // Ensure BUSY is low before asserting reset (best practice)
    (void)sx1281_wait_busy_low(handle, 100);

    gpio_set_level(handle->cfg.pin_reset, 0);
    vTaskDelay(pdMS_TO_TICKS(5));   // > 1 ms
    gpio_set_level(handle->cfg.pin_reset, 1);
    // Datasheet: tXOSCStartup ~ < few ms; wait BUSY low
    esp_err_t err = sx1281_wait_busy_low(handle, 100);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "BUSY didn't go low after reset: %s", esp_err_to_name(err));
    }
    return ESP_OK;
}

esp_err_t sx1281_create(const sx1281_config_t *cfg, sx1281_handle_t *out_handle) {
    if (!cfg || !out_handle) return ESP_ERR_INVALID_ARG;
    *out_handle = NULL;

    esp_err_t err = sx1281_spi_init_bus_if_needed(cfg);
    if (err != ESP_OK) return err;

    sx1281_handle_t dev = (sx1281_handle_t)calloc(1, sizeof(struct sx1281_dev));
    if (!dev) return ESP_ERR_NO_MEM;
    dev->cfg = *cfg;

    sx1281_gpio_init(&dev->cfg);

    err = sx1281_spi_add_device(dev);
    if (err != ESP_OK) {
        free(dev);
        return err;
    }

    // Hardware reset to bring chip into known state
    (void)sx1281_hw_reset(dev);

    *out_handle = dev;
    ESP_LOGI(TAG, "SX1281 created on SPI host %d, CS=%d", cfg->spi_host, cfg->pin_cs);
    return ESP_OK;
}

void sx1281_destroy(sx1281_handle_t handle) {
    if (!handle) return;
    if (handle->spi) {
        spi_bus_remove_device(handle->spi);
        handle->spi = NULL;
    }
    free(handle);
}

static esp_err_t sx1281_spi_transfer(sx1281_handle_t handle, const void *tx, void *rx, size_t len) {
    spi_transaction_t t = {
        .flags = 0,
        .length = (int)(len * 8),
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    return spi_device_transmit(handle->spi, &t);
}

esp_err_t sx1281_write_cmd(sx1281_handle_t handle, uint8_t opcode, const uint8_t *data, size_t len) {
    if (!handle) return ESP_ERR_INVALID_ARG;
    esp_err_t err = sx1281_wait_busy_low(handle, 100);
    if (err != ESP_OK) return err;

    uint8_t hdr = opcode;
    err = sx1281_spi_transfer(handle, &hdr, NULL, 1);
    if (err != ESP_OK) return err;
    if (len && data) {
        err = sx1281_spi_transfer(handle, data, NULL, len);
        if (err != ESP_OK) return err;
    }
    // Wait end of command (BUSY low)
    err = sx1281_wait_busy_low(handle, 100);
    return err;
}

esp_err_t sx1281_read_cmd(sx1281_handle_t handle, uint8_t opcode, uint8_t *data, size_t len) {
    if (!handle) return ESP_ERR_INVALID_ARG;
    esp_err_t err = sx1281_wait_busy_low(handle, 100);
    if (err != ESP_OK) return err;

    uint8_t tx_hdr[2] = { opcode, 0x00 }; // second byte clocks out first status/response per Semtech convention
    uint8_t rx_hdr[2] = { 0 };
    err = sx1281_spi_transfer(handle, tx_hdr, rx_hdr, sizeof(tx_hdr));
    if (err != ESP_OK) return err;

    if (len && data) {
        // clock out the remaining bytes
        memset(data, 0, len);
        err = sx1281_spi_transfer(handle, data, data, len);
        if (err != ESP_OK) return err;
    }
    // Wait end of command
    err = sx1281_wait_busy_low(handle, 100);
    return err;
}

esp_err_t sx1281_get_status(sx1281_handle_t handle, uint8_t *out_status) {
    if (!handle || !out_status) return ESP_ERR_INVALID_ARG;
    uint8_t tx[2] = { SX1281_CMD_GET_STATUS, 0x00 };
    uint8_t rx[2] = { 0 };
    esp_err_t err = sx1281_wait_busy_low(handle, 100);
    if (err != ESP_OK) return err;

    err = sx1281_spi_transfer(handle, tx, rx, sizeof(tx));
    if (err != ESP_OK) return err;

    // rx[1] is typically the status byte after the dummy
    *out_status = rx[1];
    // End of command wait
    (void)sx1281_wait_busy_low(handle, 100);
    return ESP_OK;
}
