#ifndef UI_CALIBRATION_H
#define UI_CALIBRATION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * @brief Create calibration and test interface
 * @param parent Parent container object
 */
void ui_calibration_create(lv_obj_t *parent);

/**
 * @brief Destroy calibration and test interface
 */
void ui_calibration_destroy(void);

#ifdef __cplusplus
}
#endif

#endif // UI_CALIBRATION_H
