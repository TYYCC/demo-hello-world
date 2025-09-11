#include "settings_manager.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char* TAG = "SETTINGS_MANAGER";
#define SETTINGS_NAMESPACE "ui_settings"

// Default values
#define DEFAULT_TRANSFER_MODE IMAGE_TRANSFER_MODE_TCP
#define DEFAULT_BACKLIGHT 80

// Global variables to hold current settings
static image_transfer_mode_t g_transfer_mode = DEFAULT_TRANSFER_MODE;
static uint8_t g_backlight = DEFAULT_BACKLIGHT;


// --- Private functions to handle NVS operations ---

static void save_settings_to_nvs() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return;
    }

    nvs_set_u8(nvs_handle, "transfer_mode", (uint8_t)g_transfer_mode);
    nvs_set_u8(nvs_handle, "backlight", g_backlight);

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS commit failed!");
    }
    nvs_close(nvs_handle);
}

static void load_settings_from_nvs() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS settings not found, using default values.");
        return;
    }

    uint8_t transfer_mode_val;
    err = nvs_get_u8(nvs_handle, "transfer_mode", &transfer_mode_val);
    if (err == ESP_OK) {
        g_transfer_mode = (image_transfer_mode_t)transfer_mode_val;
    }

    uint8_t backlight_val;
    err = nvs_get_u8(nvs_handle, "backlight", &backlight_val);
    if (err == ESP_OK) {
        g_backlight = backlight_val;
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "'backlight' not found in NVS, using default and saving.");
        // Value not found, so we save the default one for the next boot
        nvs_close(nvs_handle); // Close read-only handle
        save_settings_to_nvs(); // This will open a R/W handle and save all current (default) settings
        return; // Return because save_settings_to_nvs already closed the handle
    }
    
    nvs_close(nvs_handle);
}


// --- Public API Implementations ---

void settings_manager_init(void) {
    load_settings_from_nvs();
    ESP_LOGI(TAG, "Settings manager initialized. Transfer mode: %d, Backlight: %d", g_transfer_mode, g_backlight);
}

void settings_set_transfer_mode(image_transfer_mode_t mode) {
    if (mode == IMAGE_TRANSFER_MODE_TCP || mode == IMAGE_TRANSFER_MODE_UDP ||
        mode == IMAGE_TRANSFER_MODE_LZ4 || mode == IMAGE_TRANSFER_MODE_JPEG) {
        if (g_transfer_mode != mode) {
            g_transfer_mode = mode;
            save_settings_to_nvs();
            ESP_LOGI(TAG, "Set transfer mode to: %d", mode);
        }
    }
}

image_transfer_mode_t settings_get_transfer_mode(void) {
    return g_transfer_mode;
}

void settings_set_backlight(uint8_t brightness) {
    if (brightness > 100) {
        brightness = 100;
    }
    if (g_backlight != brightness) {
        g_backlight = brightness;
        save_settings_to_nvs();
        ESP_LOGI(TAG, "Set backlight to: %d", brightness);
    }
}

uint8_t settings_get_backlight(void) {
    return g_backlight;
}
