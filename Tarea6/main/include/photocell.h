#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PHOTOCELL_CLEAR,
    PHOTOCELL_BLOCKED,
    PHOTOCELL_ERROR
} photocell_state_t;

typedef struct {
    photocell_state_t state;
    uint32_t blocked_time_ms;
    uint32_t clear_count;
    uint32_t blocked_count;
} photocell_t;

void photocell_init(photocell_t *pc);
photocell_state_t photocell_update(photocell_t *pc, bool raw_level, uint32_t dt_ms);
bool photocell_is_safe(photocell_t *pc);
