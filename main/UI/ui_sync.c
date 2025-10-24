#include "ui_sync.h"
#include "esp_log.h"

static const char *TAG = "UI_SYNC";
static EventGroupHandle_t s_ui_event_group = NULL;

void ui_sync_init(void) {
    if (s_ui_event_group == NULL) {
        s_ui_event_group = xEventGroupCreate();
        if (!s_ui_event_group) {
            ESP_LOGE(TAG, "Failed to create UI event group");
        }
    }
}

void ui_signal_start_anim_ready(void) {
    if (s_ui_event_group == NULL) {
        ui_sync_init();
    }
    if (s_ui_event_group) {
        xEventGroupSetBits(s_ui_event_group, UI_EVT_START_ANIM_READY_BIT);
        ESP_LOGI(TAG, "Start animation ready signaled");
    }
}

bool ui_wait_start_anim_ready(TickType_t ticks_to_wait) {
    if (s_ui_event_group == NULL) {
        ui_sync_init();
    }
    if (!s_ui_event_group) {
        return false;
    }
    EventBits_t bits = xEventGroupWaitBits(
        s_ui_event_group,
        UI_EVT_START_ANIM_READY_BIT,
        pdFALSE,   // don't clear on exit
        pdFALSE,   // wait for any bit
        ticks_to_wait
    );
    return (bits & UI_EVT_START_ANIM_READY_BIT) != 0;
}
