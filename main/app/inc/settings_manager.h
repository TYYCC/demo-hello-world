#ifndef SETTINGS_MANAGER_H
#define SETTINGS_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Enum for image transfer mode
typedef enum {
    IMAGE_TRANSFER_MODE_TCP,
    IMAGE_TRANSFER_MODE_UDP,
    IMAGE_TRANSFER_MODE_LZ4,   // LZ4解码模式
    IMAGE_TRANSFER_MODE_JPEG   // JPEG解码模式
} image_transfer_mode_t;

/**
 * @brief Initializes the settings manager.
 *        (Could be used to load settings from NVS in the future)
 */
void settings_manager_init(void);

/**
 * @brief Sets the current image transfer mode.
 * 
 * @param mode The new mode to set.
 */
void settings_set_transfer_mode(image_transfer_mode_t mode);

/**
 * @brief Gets the current image transfer mode.
 * 
 * @return The currently configured image_transfer_mode_t.
 */
image_transfer_mode_t settings_get_transfer_mode(void);


/**
 * @brief 设置屏幕背光亮度
 * @param brightness 亮度 (0-100)
 */
void settings_set_backlight(uint8_t brightness);

/**
 * @brief 获取屏幕背光亮度
 * @return 亮度 (0-100)
 */
uint8_t settings_get_backlight(void);


#ifdef __cplusplus
}
#endif

#endif // SETTINGS_MANAGER_H
