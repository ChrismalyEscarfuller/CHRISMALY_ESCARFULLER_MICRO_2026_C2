#include "led_handler.h"
#include "config.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "LEDS";

static void led_init_pin(gpio_num_t pin)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(pin, 0);
}

void led_handler_init(void)
{
    led_init_pin(LED_OPEN_PIN);
    led_init_pin(LED_CLOSE_PIN);
    led_init_pin(LED_ERROR_PIN);
    led_init_pin(LED_CONN_PIN);
    ESP_LOGI(TAG, "LEDs initialized (Open:GPIO%d, Close:GPIO%d, Err:GPIO%d, Conn:GPIO%d)",
             LED_OPEN_PIN, LED_CLOSE_PIN, LED_ERROR_PIN, LED_CONN_PIN);
}

void led_open_on(void) { gpio_set_level(LED_OPEN_PIN, 1); }
void led_open_off(void) { gpio_set_level(LED_OPEN_PIN, 0); }
void led_close_on(void) { gpio_set_level(LED_CLOSE_PIN, 1); }
void led_close_off(void) { gpio_set_level(LED_CLOSE_PIN, 0); }
void led_error_on(void) { gpio_set_level(LED_ERROR_PIN, 1); }
void led_error_off(void) { gpio_set_level(LED_ERROR_PIN, 0); }
void led_conn_on(void) { gpio_set_level(LED_CONN_PIN, 1); }
void led_conn_off(void) { gpio_set_level(LED_CONN_PIN, 0); }
void led_conn_toggle(void) { gpio_set_level(LED_CONN_PIN, !gpio_get_level(LED_CONN_PIN)); }

void led_set_state(bool opening, bool closing, bool error, bool connected)
{
    if (opening) { led_open_on(); led_close_off(); }
    else if (closing) { led_open_off(); led_close_on(); }
    else { led_open_off(); led_close_off(); }

    if (error) led_error_on(); else led_error_off();
    if (connected) led_conn_on(); else led_conn_off();
}
