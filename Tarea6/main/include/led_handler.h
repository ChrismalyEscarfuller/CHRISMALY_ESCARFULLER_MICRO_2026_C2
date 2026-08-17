#pragma once

#include <stdbool.h>

void led_handler_init(void);
void led_open_on(void);
void led_open_off(void);
void led_close_on(void);
void led_close_off(void);
void led_error_on(void);
void led_error_off(void);
void led_conn_on(void);
void led_conn_off(void);
void led_conn_toggle(void);
void led_set_state(bool opening, bool closing, bool error, bool connected);
