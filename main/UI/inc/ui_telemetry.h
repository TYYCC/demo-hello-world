#ifndef UI_TELEMETRY_H
#define UI_TELEMETRY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include <stdint.h>

void ui_telemetry_create(lv_obj_t* parent);

/**
 * @brief 更新遥测UI的滑动条值
 * @param throttle 油门值 (0-1000)
 * @param direction 方向值 (0-1000)
 */
void ui_telemetry_update_sliders(int32_t throttle, int32_t direction);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*UI_TELEMETRY_H*/
