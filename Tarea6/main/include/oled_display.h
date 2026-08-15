#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t width;
    uint8_t height;
    bool initialized;
} oled_t;

void oled_init(oled_t *oled);
void oled_clear(void);
void oled_draw_string(int x, int y, const char *str);
void oled_draw_line(int x0, int y0, int x1, int y1);
void oled_update(void);
void oled_show_status(const char *state_str, bool wifi_ok, bool mqtt_ok, const char *extra);
void oled_show_error(const char *msg);
