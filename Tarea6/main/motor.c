#include "motor.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "MOTOR";
static motor_direction_t current_direction = MOTOR_STOP;

static void pwm_stop_all(void)
{
    ledc_set_duty(MOTOR_PWM_MODE, MOTOR_PWM_CH_FORWARD, 0);
    ledc_update_duty(MOTOR_PWM_MODE, MOTOR_PWM_CH_FORWARD);
    ledc_set_duty(MOTOR_PWM_MODE, MOTOR_PWM_CH_BACKWARD, 0);
    ledc_update_duty(MOTOR_PWM_MODE, MOTOR_PWM_CH_BACKWARD);
}

void motor_init(void)
{
    ledc_timer_config_t timer_conf = {
        .speed_mode = MOTOR_PWM_MODE,
        .duty_resolution = MOTOR_PWM_RES,
        .timer_num = MOTOR_PWM_TIMER,
        .freq_hz = MOTOR_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    ledc_channel_config_t ch_forward = {
        .gpio_num = MOTOR_FORWARD_PIN,
        .speed_mode = MOTOR_PWM_MODE,
        .channel = MOTOR_PWM_CH_FORWARD,
        .timer_sel = MOTOR_PWM_TIMER,
        .duty = 0,
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_forward));

    ledc_channel_config_t ch_backward = {
        .gpio_num = MOTOR_BACKWARD_PIN,
        .speed_mode = MOTOR_PWM_MODE,
        .channel = MOTOR_PWM_CH_BACKWARD,
        .timer_sel = MOTOR_PWM_TIMER,
        .duty = 0,
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_backward));

    current_direction = MOTOR_STOP;
    ESP_LOGI(TAG, "Motor PWM initialized (Fwd:GPIO%d, Bwd:GPIO%d)",
             MOTOR_FORWARD_PIN, MOTOR_BACKWARD_PIN);
}

void motor_set_direction(motor_direction_t dir)
{
    pwm_stop_all();

    switch (dir) {
    case MOTOR_FORWARD:
        ledc_set_duty(MOTOR_PWM_MODE, MOTOR_PWM_CH_FORWARD, MOTOR_SPEED_MID);
        ledc_update_duty(MOTOR_PWM_MODE, MOTOR_PWM_CH_FORWARD);
        ESP_LOGI(TAG, "Motor -> FORWARD");
        break;
    case MOTOR_BACKWARD:
        ledc_set_duty(MOTOR_PWM_MODE, MOTOR_PWM_CH_BACKWARD, MOTOR_SPEED_MID);
        ledc_update_duty(MOTOR_PWM_MODE, MOTOR_PWM_CH_BACKWARD);
        ESP_LOGI(TAG, "Motor -> BACKWARD");
        break;
    case MOTOR_STOP:
        ESP_LOGI(TAG, "Motor -> STOP");
        break;
    }
    current_direction = dir;
}

void motor_set_speed(uint32_t speed)
{
    if (speed > MOTOR_SPEED_MAX) speed = MOTOR_SPEED_MAX;
    switch (current_direction) {
    case MOTOR_FORWARD:
        ledc_set_duty(MOTOR_PWM_MODE, MOTOR_PWM_CH_FORWARD, speed);
        ledc_update_duty(MOTOR_PWM_MODE, MOTOR_PWM_CH_FORWARD);
        break;
    case MOTOR_BACKWARD:
        ledc_set_duty(MOTOR_PWM_MODE, MOTOR_PWM_CH_BACKWARD, speed);
        ledc_update_duty(MOTOR_PWM_MODE, MOTOR_PWM_CH_BACKWARD);
        break;
    default:
        break;
    }
}

motor_direction_t motor_get_direction(void)
{
    return current_direction;
}

void motor_stop(void)
{
    motor_set_direction(MOTOR_STOP);
}
