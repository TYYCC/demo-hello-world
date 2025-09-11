/*
 * @Author: tidycraze 2595256284@qq.com
 * @Date: 2025-09-10 13:01:26
 * @LastEditors: tidycraze 2595256284@qq.com
 * @LastEditTime: 2025-09-11 17:00:15
 * @FilePath: \demo-hello-world\main\app\image_transfer\src\ui_mapping_service.c
 * @Description: 
 * 
 */
// Deprecated after refactor: kept as empty stubs to avoid broad include churn
#include "ui_mapping_service.h"
#include "esp_err.h"
#include <stdbool.h>

esp_err_t ui_mapping_service_init(ui_mapping_callback_t ui_callback, void* context) {
    return ESP_OK;
}
void ui_mapping_service_deinit(void) {}
bool ui_mapping_service_is_running(void) { return false; }
EventGroupHandle_t ui_mapping_service_get_event_group(void) { return NULL; }
lz4_decoder_callback_t ui_mapping_service_get_lz4_callback(void) { return NULL; }
jpeg_decoder_callback_t ui_mapping_service_get_jpeg_callback(void) { return NULL; }
void ui_mapping_service_frame_unlock(void) {}