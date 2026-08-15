#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "config.h"
#include "state_machine.h"
#include "motor.h"
#include "mqtt_handler.h"
#include "oled_display.h"
#include "input_handler.h"
#include "buzzer.h"
#include "led_handler.h"
#include "photocell.h"

static const char *TAG = "MAIN";

static state_machine_t sm;
static mqtt_handler_t mqtt;
static oled_t oled;
static photocell_t photocell;
static QueueHandle_t event_queue;
static bool mqtt_parse_cmd(const char *data, int len, gate_event_t *out);

static void mqtt_command_cb(const char *data, int len)
{
    if (!event_queue || !data || len <= 0) return;

    gate_event_t event;
    if (mqtt_parse_cmd(data, len, &event)) {
        xQueueSend(event_queue, &event, 0);
    }
}

static bool mqtt_parse_cmd(const char *data, int len, gate_event_t *out)
{
    if (len >= 4 && strncmp(data, "open", 4) == 0) {
        *out = EVENT_OPEN_CMD; return true;
    }
    if (len >= 5 && strncmp(data, "close", 5) == 0) {
        *out = EVENT_CLOSE_CMD; return true;
    }
    if (len >= 4 && strncmp(data, "stop", 4) == 0) {
        *out = EVENT_STOP_CMD; return true;
    }
    if (len >= 5 && strncmp(data, "reset", 5) == 0) {
        *out = EVENT_RESET; return true;
    }
    if (len >= 9 && strncmp(data, "calibrate", 9) == 0) {
        *out = EVENT_CALIBRATE; return true;
    }
    return false;
}

static void publish_status(gate_state_t state)
{
    const char *s = sm_state_str(state);
    mqtt_handler_publish(MQTT_TOPIC_STATUS, s);
}

static void publish_error(const char *msg)
{
    mqtt_handler_publish(MQTT_TOPIC_ERROR, msg);
}

static void handle_error(state_machine_t *sm_ptr, const char *reason)
{
    ESP_LOGE(TAG, "ERROR: %s", reason);
    motor_stop();
    buzzer_error_on();
    led_set_state(false, false, true, mqtt.connected);
    oled_show_error(reason);
    publish_error(reason);
}

static void do_open(state_machine_t *sm_ptr)
{
    ESP_LOGI(TAG, "Executing OPEN");
    motor_set_direction(MOTOR_FORWARD);
    led_set_state(true, false, false, mqtt.connected);
}

static void do_close(state_machine_t *sm_ptr)
{
    ESP_LOGI(TAG, "Executing CLOSE");
    motor_set_direction(MOTOR_BACKWARD);
    led_set_state(false, true, false, mqtt.connected);
}

static void do_stop(state_machine_t *sm_ptr)
{
    ESP_LOGI(TAG, "Executing STOP");
    motor_stop();
    led_set_state(false, false, sm_is_error(sm_ptr->current_state), mqtt.connected);
}

static void do_reverse(state_machine_t *sm_ptr)
{
    ESP_LOGW(TAG, "REVERSING due to photocell");
    motor_set_direction(MOTOR_FORWARD);
    led_error_on();
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== PORTON AUTOMATICO v1.0 ===");

    event_queue = xQueueCreate(16, sizeof(gate_event_t));
    if (!event_queue) {
        ESP_LOGE(TAG, "Failed to create event queue");
        return;
    }

    oled_init(&oled);
    oled_show_status("INIT", false, false, "Starting...");

    led_handler_init();
    buzzer_init();
    motor_init();
    sm_init(&sm);

    photocell_init(&photocell);

    input_handler_init(event_queue);

    mqtt_handler_init(&mqtt);
    mqtt_handler_set_callback(mqtt_command_cb);
    mqtt_handler_start(&mqtt);

    input_handler_start_timer();

    oled_show_status("IDLE", false, false, "Waiting...");

    uint32_t last_tick_ms = 0;
    uint32_t buzzer_timer = 0;
    uint32_t motor_timeout = 0;
    uint32_t reverse_timer = 0;
    uint32_t auto_close_timer = 0;

    gate_event_t event;

    while (1) {
        if (xQueueReceive(event_queue, &event, pdMS_TO_TICKS(50))) {
            uint32_t now = esp_timer_get_time() / 1000;
            uint32_t dt = now - last_tick_ms;
            last_tick_ms = now;

            sm.state_time_ms += dt;

            switch (event) {
            case EVENT_OPEN_CMD:
                sm_process_event(&sm, event);
                if (sm.current_state == STATE_OPENING) {
                    do_open(&sm);
                    motor_timeout = sm.state_time_ms;
                }
                break;

            case EVENT_CLOSE_CMD:
                sm_process_event(&sm, event);
                if (sm.current_state == STATE_CLOSING) {
                    do_close(&sm);
                    motor_timeout = sm.state_time_ms;
                }
                break;

            case EVENT_STOP_CMD:
                sm_process_event(&sm, event);
                if (sm.current_state == STATE_STOPPED)
                    do_stop(&sm);
                break;

            case EVENT_LIMIT_OPEN:
                sm_process_event(&sm, event);
                if (sm.current_state == STATE_OPEN) {
                    motor_stop();
                    led_set_state(false, false, false, mqtt.connected);
                    auto_close_timer = sm.state_time_ms;
                }
                break;

            case EVENT_LIMIT_CLOSE:
                sm_process_event(&sm, event);
                if (sm.current_state == STATE_CLOSED) {
                    motor_stop();
                    led_set_state(false, false, false, mqtt.connected);
                }
                break;

            case EVENT_PHOTOCELL_ON:
                sm.photocell_blocked = true;
                if (sm.current_state == STATE_CLOSING) {
                    sm_process_event(&sm, event);
                    if (sm.current_state == STATE_REVERSING) {
                        do_reverse(&sm);
                        reverse_timer = sm.state_time_ms;
                    }
                }
                break;

            case EVENT_PHOTOCELL_OFF:
                sm.photocell_blocked = false;
                break;

            case EVENT_ERROR:
                sm_process_event(&sm, event);
                handle_error(&sm, "Fault detected");
                break;

            case EVENT_RESET:
                sm_process_event(&sm, event);
                if (sm.current_state == STATE_IDLE) {
                    buzzer_off();
                    motor_stop();
                    led_set_state(false, false, false, mqtt.connected);
                    oled_show_status("IDLE", mqtt.connected, mqtt.connected, "Reset OK");
                }
                break;

            case EVENT_CALIBRATE:
                sm_process_event(&sm, event);
                if (sm.current_state == STATE_CALIBRATING) {
                    motor_set_direction(MOTOR_BACKWARD);
                    oled_show_status("CALIB", false, false, "Closing...");
                }
                break;

            case EVENT_REVERSE_DONE:
                sm_process_event(&sm, event);
                if (sm.current_state == STATE_OPENING) {
                    do_open(&sm);
                    motor_timeout = sm.state_time_ms;
                }
                break;

            case EVENT_AUTO_CLOSE:
                sm_process_event(&sm, event);
                if (sm.current_state == STATE_CLOSING) {
                    do_close(&sm);
                    motor_timeout = sm.state_time_ms;
                }
                break;

            case EVENT_MOTOR_TIMEOUT:
                sm_process_event(&sm, event);
                handle_error(&sm, "Motor timeout");
                break;
            }

            publish_status(sm.current_state);
            oled_show_status(sm_state_str(sm.current_state), mqtt.connected, mqtt.connected,
                             sm.photocell_blocked ? "Photo BLOCKED" : "");
        }

        uint32_t now = esp_timer_get_time() / 1000;
        uint32_t dt = now - last_tick_ms;
        last_tick_ms = now;
        sm.state_time_ms += dt;

        if (sm_is_error(sm.current_state)) {
            buzzer_timer += dt;
            if (buzzer_timer >= BUZZER_ERROR_MS) {
                buzzer_off();
                buzzer_timer = 0;
            }
        }

        if (sm.current_state == STATE_OPENING || sm.current_state == STATE_CLOSING) {
            if ((sm.state_time_ms - motor_timeout) >= MOTOR_TIMEOUT_MS) {
                gate_event_t e = EVENT_MOTOR_TIMEOUT;
                xQueueSend(event_queue, &e, 0);
            }
        }

        if (sm.current_state == STATE_REVERSING) {
            if ((sm.state_time_ms - reverse_timer) >= REVERSE_DURATION_MS) {
                gate_event_t e = EVENT_REVERSE_DONE;
                xQueueSend(event_queue, &e, 0);
            }
        }

        if (sm.current_state == STATE_OPEN) {
            if ((sm.state_time_ms - auto_close_timer) >= AUTO_CLOSE_MS) {
                gate_event_t e = EVENT_AUTO_CLOSE;
                xQueueSend(event_queue, &e, 0);
            }
        }

        inputs_t inputs = input_handler_get_last();
        photocell_update(&photocell, inputs.photocell, dt);
    }
}
