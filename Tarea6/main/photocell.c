#include "photocell.h"
#include "config.h"
#include "esp_log.h"

static const char *TAG = "PHOTOCELL";

void photocell_init(photocell_t *pc)
{
    pc->state = PHOTOCELL_CLEAR;
    pc->blocked_time_ms = 0;
    pc->clear_count = 0;
    pc->blocked_count = 0;
    ESP_LOGI(TAG, "Photocell initialized");
}

photocell_state_t photocell_update(photocell_t *pc, bool raw_level, uint32_t dt_ms)
{
    if (raw_level) {
        pc->blocked_time_ms += dt_ms;
        if (pc->blocked_time_ms >= PHOTOCELL_DEBOUNCE_MS) {
            if (pc->state != PHOTOCELL_BLOCKED) {
                pc->state = PHOTOCELL_BLOCKED;
                pc->blocked_count++;
                ESP_LOGW(TAG, "Photocell BLOCKED (count=%lu)", pc->blocked_count);
            }
        }
    } else {
        if (pc->state == PHOTOCELL_BLOCKED && pc->blocked_time_ms >= PHOTOCELL_DEBOUNCE_MS) {
            pc->state = PHOTOCELL_CLEAR;
            pc->clear_count++;
            ESP_LOGI(TAG, "Photocell CLEAR (count=%lu)", pc->clear_count);
        }
        pc->blocked_time_ms = 0;
    }

    return pc->state;
}

bool photocell_is_safe(photocell_t *pc)
{
    return pc->state == PHOTOCELL_CLEAR;
}
