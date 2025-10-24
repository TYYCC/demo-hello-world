#ifndef UI_SYNC_H
#define UI_SYNC_H

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

// Bit for start animation ready
#define UI_EVT_START_ANIM_READY_BIT    (1 << 0)

// Initialize event group (called lazily inside functions)
void ui_sync_init(void);

// Signal from LVGL thread that start animation is ready (safe from LVGL thread)
void ui_signal_start_anim_ready(void);

// Wait for start animation ready; returns true if ready within ticks_to_wait
bool ui_wait_start_anim_ready(TickType_t ticks_to_wait);

#ifdef __cplusplus
}
#endif

#endif // UI_SYNC_H
