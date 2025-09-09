#ifndef WIFI_IMAGE_TRANSFER_H
#define WIFI_IMAGE_TRANSFER_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "esp_jpeg_dec.h"
#include "freertos/event_groups.h"
#include "settings_manager.h"


#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and start the Wi-Fi TCP image transfer server.
 *
 * @param port The port number to listen on.
 * @return true if the server started successfully, false otherwise.
 */
bool wifi_image_transfer_start(uint16_t port);

/**
 * @brief Stop the WiFi image transfer TCP server.
 */
void wifi_image_transfer_stop(void);

/**
 * @brief Get the UI event group handle.
 *
 * @return The EventGroupHandle_t for UI events.
 */
EventGroupHandle_t wifi_image_transfer_get_ui_event_group(void);

/**
 * @brief Get the latest decoded frame buffer.
 *
 * This function locks a mutex to provide exclusive access to the frame buffer.
 * You MUST call wifi_image_transfer_frame_unlock() after you are done with the buffer.
 *
 * @param buffer Pointer to receive the frame buffer address.
 * @param width Pointer to receive the frame width.
 * @param height Pointer to receive the frame height.
 * @return ESP_OK on success, ESP_FAIL otherwise.
 */
esp_err_t wifi_image_transfer_get_latest_frame(uint8_t** buffer, int* width, int* height);

/**
 * @brief Unlock the frame buffer mutex.
 *
 * This must be called after processing the frame obtained from wifi_image_transfer_get_latest_frame().
 */
void wifi_image_transfer_frame_unlock(void);

/**
 * @brief Get the current frames per second (FPS) of image rendering.
 *
 * @return The current FPS rate.
 */
float wifi_image_transfer_get_fps(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_IMAGE_TRANSFER_H