#include "buzzer.h"
#include "config.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "BUZZER";
static bool active = false;

void buzzer_init(void)
{
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .timer_num = LEDC_TIMER_1,
        .freq_hz = BUZZER_FREQ_ERROR,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    ledc_channel_config_t ch = {
        .gpio_num = BUZZER_PIN,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BUZZER_PWM_CH,
        .timer_sel = LEDC_TIMER_1,
        .duty = 0,
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch));

    ESP_LOGI(TAG, "Buzzer initialized on GPIO%d", BUZZER_PIN);
}

void buzzer_error_on(void)
{
    ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_1, BUZZER_FREQ_ERROR);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZER_PWM_CH, 4096);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZER_PWM_CH);
    active = true;
    ESP_LOGI(TAG, "Buzzer ERROR on");
}

void buzzer_warn_on(void)
{
    ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_1, BUZZER_FREQ_WARN);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZER_PWM_CH, 2048);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZER_PWM_CH);
    active = true;
    ESP_LOGI(TAG, "Buzzer WARN on");
}

void buzzer_off(void)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZER_PWM_CH, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZER_PWM_CH);
    active = false;
}

bool buzzer_is_active(void)
{
    return active;
}
