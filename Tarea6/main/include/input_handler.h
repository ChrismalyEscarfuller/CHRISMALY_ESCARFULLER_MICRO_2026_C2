#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef struct {
    bool btn_open;
    bool btn_close;
    bool btn_stop;
    bool limit_open;
    bool limit_close;
    bool photocell;
} inputs_t;

void input_handler_init(QueueHandle_t event_queue);
inputs_t input_handler_get_last(void);
void input_handler_start_timer(void);
