#include "input_handler.h"
#include "config.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "state_machine.h"

static const char *TAG = "INPUT";
static QueueHandle_t evt_queue = NULL;
static inputs_t last_inputs = {0};
static inputs_t debounced = {0};
static inputs_t prev_debounced = {0};
static uint32_t debounce_counters[6] = {0};

#define PIN_COUNT 6
static const int pin_map[PIN_COUNT] = {
    BTN_OPEN_PIN, BTN_CLOSE_PIN, BTN_STOP_PIN,
    LIMIT_OPEN_PIN, LIMIT_CLOSE_PIN, PHOTOCELL_PIN
};

static bool read_raw(int idx)
{
    return gpio_get_level(pin_map[idx]) == 0;
}

static void input_scan_timer_cb(void *arg)
{
    bool raw[PIN_COUNT];
    for (int i = 0; i < PIN_COUNT; i++)
        raw[i] = read_raw(i);

    bool *dest[] = {&debounced.btn_open, &debounced.btn_close, &debounced.btn_stop,
                    &debounced.limit_open, &debounced.limit_close, &debounced.photocell};

    for (int i = 0; i < PIN_COUNT; i++) {
        if (raw[i] == *dest[i]) {
            debounce_counters[i] = 0;
        } else {
            debounce_counters[i]++;
            if (debounce_counters[i] >= (DEBOUNCE_MS / TIMER_SCAN_MS)) {
                *dest[i] = raw[i];
                debounce_counters[i] = 0;
            }
        }
    }

    last_inputs = debounced;

    if (!evt_queue) return;

    if (debounced.btn_open && !prev_debounced.btn_open) {
        gate_event_t e = EVENT_OPEN_CMD;
        xQueueSendFromISR(evt_queue, &e, NULL);
    }
    if (debounced.btn_close && !prev_debounced.btn_close) {
        gate_event_t e = EVENT_CLOSE_CMD;
        xQueueSendFromISR(evt_queue, &e, NULL);
    }
    if (debounced.btn_stop && !prev_debounced.btn_stop) {
        gate_event_t e = EVENT_STOP_CMD;
        xQueueSendFromISR(evt_queue, &e, NULL);
    }
    if (debounced.limit_open && !prev_debounced.limit_open) {
        gate_event_t e = EVENT_LIMIT_OPEN;
        xQueueSendFromISR(evt_queue, &e, NULL);
    }
    if (debounced.limit_close && !prev_debounced.limit_close) {
        gate_event_t e = EVENT_LIMIT_CLOSE;
        xQueueSendFromISR(evt_queue, &e, NULL);
    }
    if (debounced.photocell && !prev_debounced.photocell) {
        gate_event_t e = EVENT_PHOTOCELL_ON;
        xQueueSendFromISR(evt_queue, &e, NULL);
    }
    if (!debounced.photocell && prev_debounced.photocell) {
        gate_event_t e = EVENT_PHOTOCELL_OFF;
        xQueueSendFromISR(evt_queue, &e, NULL);
    }

    prev_debounced = debounced;
}

void input_handler_init(QueueHandle_t event_queue)
{
    evt_queue = event_queue;

    for (int i = 0; i < PIN_COUNT; i++) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << pin_map[i]),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&io_conf);
    }

    memset(&debounced, 0, sizeof(debounced));
    memset(&prev_debounced, 0, sizeof(prev_debounced));
    memset(debounce_counters, 0, sizeof(debounce_counters));

    ESP_LOGI(TAG, "Inputs initialized (50ms scan timer)");
}

inputs_t input_handler_get_last(void)
{
    return last_inputs;
}

void input_handler_start_timer(void)
{
    const esp_timer_create_args_t timer_args = {
        .callback = input_scan_timer_cb,
        .name = "input_scan"
    };
    esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, TIMER_SCAN_MS * 1000));
}
