#include "state_machine.h"
#include "esp_log.h"

static const char *TAG = "STATE_MACHINE";

static const char *state_names[] = {
    "IDLE", "OPENING", "CLOSING", "STOPPED",
    "OPEN", "CLOSED", "REVERSING", "ERROR", "CALIBRATING"
};

static const char *event_names[] = {
    "OPEN_CMD", "CLOSE_CMD", "STOP_CMD", "LIMIT_OPEN",
    "LIMIT_CLOSE", "PHOTOCELL_ON", "PHOTOCELL_OFF", "ERROR",
    "RESET", "CALIBRATE", "REVERSE_DONE", "AUTO_CLOSE", "MOTOR_TIMEOUT"
};

typedef struct {
    gate_state_t current;
    gate_event_t event;
    gate_state_t next;
} transition_t;

static const transition_t transition_table[] = {
    {STATE_IDLE,       EVENT_OPEN_CMD,     STATE_OPENING},
    {STATE_IDLE,       EVENT_CALIBRATE,    STATE_CALIBRATING},
    {STATE_CLOSED,     EVENT_OPEN_CMD,     STATE_OPENING},
    {STATE_CLOSED,     EVENT_CALIBRATE,    STATE_CALIBRATING},
    {STATE_OPENING,    EVENT_LIMIT_OPEN,   STATE_OPEN},
    {STATE_OPENING,    EVENT_STOP_CMD,     STATE_STOPPED},
    {STATE_OPENING,    EVENT_ERROR,        STATE_ERROR},
    {STATE_OPENING,    EVENT_MOTOR_TIMEOUT,STATE_ERROR},
    {STATE_CLOSING,    EVENT_LIMIT_CLOSE,  STATE_CLOSED},
    {STATE_CLOSING,    EVENT_STOP_CMD,     STATE_STOPPED},
    {STATE_CLOSING,    EVENT_PHOTOCELL_ON, STATE_REVERSING},
    {STATE_CLOSING,    EVENT_ERROR,        STATE_ERROR},
    {STATE_CLOSING,    EVENT_MOTOR_TIMEOUT,STATE_ERROR},
    {STATE_STOPPED,    EVENT_OPEN_CMD,     STATE_OPENING},
    {STATE_STOPPED,    EVENT_CLOSE_CMD,    STATE_CLOSING},
    {STATE_STOPPED,    EVENT_ERROR,        STATE_ERROR},
    {STATE_OPEN,       EVENT_CLOSE_CMD,    STATE_CLOSING},
    {STATE_OPEN,       EVENT_AUTO_CLOSE,   STATE_CLOSING},
    {STATE_OPEN,       EVENT_ERROR,        STATE_ERROR},
    {STATE_REVERSING,  EVENT_REVERSE_DONE, STATE_OPENING},
    {STATE_REVERSING,  EVENT_STOP_CMD,     STATE_STOPPED},
    {STATE_REVERSING,  EVENT_ERROR,        STATE_ERROR},
    {STATE_REVERSING,  EVENT_LIMIT_OPEN,   STATE_OPEN},
    {STATE_ERROR,      EVENT_RESET,        STATE_IDLE},
    {STATE_CALIBRATING,EVENT_LIMIT_CLOSE,  STATE_IDLE},
    {STATE_CALIBRATING,EVENT_LIMIT_OPEN,   STATE_IDLE},
    {STATE_CALIBRATING,EVENT_ERROR,        STATE_ERROR},
    {STATE_CALIBRATING,EVENT_RESET,        STATE_IDLE},
};

static const int transition_count = sizeof(transition_table) / sizeof(transition_t);

void sm_init(state_machine_t *sm)
{
    sm->current_state = STATE_IDLE;
    sm->state_time_ms = 0;
    sm->photocell_blocked = false;
    sm->open_timeout_count = 0;
    sm->close_timeout_count = 0;
    ESP_LOGI(TAG, "State machine initialized -> IDLE");
}

void sm_process_event(state_machine_t *sm, gate_event_t event)
{
    for (int i = 0; i < transition_count; i++) {
        const transition_t *t = &transition_table[i];
        if (t->current == sm->current_state && t->event == event) {
            ESP_LOGI(TAG, "%s + %s -> %s",
                     state_names[sm->current_state],
                     event_names[event],
                     state_names[t->next]);
            sm->current_state = t->next;
            sm->state_time_ms = 0;
            return;
        }
    }
    ESP_LOGW(TAG, "No transition: %s + %s (ignored)",
             state_names[sm->current_state], event_names[event]);
}

const char *sm_state_str(gate_state_t state)
{
    if (state < 0 || state >= sizeof(state_names)/sizeof(state_names[0]))
        return "UNKNOWN";
    return state_names[state];
}

const char *sm_event_str(gate_event_t event)
{
    if (event < 0 || event >= sizeof(event_names)/sizeof(event_names[0]))
        return "UNKNOWN_EVENT";
    return event_names[event];
}

bool sm_is_motor_running(gate_state_t state)
{
    return (state == STATE_OPENING || state == STATE_CLOSING || state == STATE_REVERSING);
}

bool sm_is_reversing(gate_state_t state)
{
    return state == STATE_REVERSING;
}

bool sm_is_error(gate_state_t state)
{
    return state == STATE_ERROR;
}
