#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    STATE_IDLE,
    STATE_OPENING,
    STATE_CLOSING,
    STATE_STOPPED,
    STATE_OPEN,
    STATE_CLOSED,
    STATE_REVERSING,
    STATE_ERROR,
    STATE_CALIBRATING
} gate_state_t;

typedef enum {
    EVENT_OPEN_CMD,
    EVENT_CLOSE_CMD,
    EVENT_STOP_CMD,
    EVENT_LIMIT_OPEN,
    EVENT_LIMIT_CLOSE,
    EVENT_PHOTOCELL_ON,
    EVENT_PHOTOCELL_OFF,
    EVENT_ERROR,
    EVENT_RESET,
    EVENT_CALIBRATE,
    EVENT_REVERSE_DONE,
    EVENT_AUTO_CLOSE,
    EVENT_MOTOR_TIMEOUT
} gate_event_t;

typedef struct {
    gate_state_t current_state;
    uint32_t state_time_ms;
    bool photocell_blocked;
    uint32_t open_timeout_count;
    uint32_t close_timeout_count;
} state_machine_t;

void sm_init(state_machine_t *sm);
void sm_process_event(state_machine_t *sm, gate_event_t event);
const char *sm_state_str(gate_state_t state);
const char *sm_event_str(gate_event_t event);
bool sm_is_motor_running(gate_state_t state);
bool sm_is_reversing(gate_state_t state);
bool sm_is_error(gate_state_t state);
