#pragma once

#include <stdint.h>
#include <stdbool.h>

void buzzer_init(void);
void buzzer_error_on(void);
void buzzer_warn_on(void);
void buzzer_off(void);
bool buzzer_is_active(void);
