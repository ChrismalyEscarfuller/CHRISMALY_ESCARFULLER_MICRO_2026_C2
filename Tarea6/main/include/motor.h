#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

typedef enum {
    MOTOR_STOP,
    MOTOR_FORWARD,
    MOTOR_BACKWARD
} motor_direction_t;

void motor_init(void);
void motor_set_direction(motor_direction_t dir);
void motor_set_speed(uint32_t speed);
motor_direction_t motor_get_direction(void);
void motor_stop(void);
