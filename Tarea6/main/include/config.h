#pragma once

#include "driver/gpio.h"
#include "driver/ledc.h"

/* ======================== MOTOR PWM ======================== */
#define MOTOR_FORWARD_PIN       GPIO_NUM_18
#define MOTOR_BACKWARD_PIN      GPIO_NUM_19
#define MOTOR_PWM_FREQ_HZ       1000
#define MOTOR_PWM_TIMER         LEDC_TIMER_0
#define MOTOR_PWM_MODE          LEDC_LOW_SPEED_MODE
#define MOTOR_PWM_CH_FORWARD    LEDC_CHANNEL_0
#define MOTOR_PWM_CH_BACKWARD   LEDC_CHANNEL_1
#define MOTOR_PWM_RES           LEDC_TIMER_13_BIT
#define MOTOR_SPEED_MAX         8191
#define MOTOR_SPEED_MID         4096
#define MOTOR_SPEED_SLOW        2048

/* ======================== ENTRADAS ======================== */
#define BTN_OPEN_PIN            GPIO_NUM_13
#define BTN_CLOSE_PIN           GPIO_NUM_14
#define BTN_STOP_PIN            GPIO_NUM_27
#define LIMIT_OPEN_PIN          GPIO_NUM_26
#define LIMIT_CLOSE_PIN         GPIO_NUM_25
#define PHOTOCELL_PIN           GPIO_NUM_33

/* ======================== BUZZER ======================== */
#define BUZZER_PIN              GPIO_NUM_32
#define BUZZER_PWM_CH           LEDC_CHANNEL_2
#define BUZZER_FREQ_ERROR       2000
#define BUZZER_FREQ_WARN        1000

/* ======================== LEDS ======================== */
#define LED_OPEN_PIN            GPIO_NUM_4
#define LED_CLOSE_PIN           GPIO_NUM_2
#define LED_ERROR_PIN           GPIO_NUM_15
#define LED_CONN_PIN            GPIO_NUM_16

/* ======================== I2C OLED ======================== */
#define I2C_MASTER_SCL_IO       GPIO_NUM_22
#define I2C_MASTER_SDA_IO       GPIO_NUM_21
#define I2C_MASTER_NUM          I2C_NUM_0
#define I2C_MASTER_FREQ_HZ      400000
#define OLED_ADDR               0x3C
#define OLED_WIDTH              128
#define OLED_HEIGHT             64

/* ======================== TIEMPOS ======================== */
#define TIMER_SCAN_MS           50
#define MOTOR_TIMEOUT_MS        30000
#define REVERSE_DURATION_MS     2000
#define AUTO_CLOSE_MS           15000
#define BUZZER_ERROR_MS         3000
#define DEBOUNCE_MS             50
#define PHOTOCELL_DEBOUNCE_MS   100

/* ======================== WIFI / MQTT ======================== */
#define WIFI_SSID               "TP-Link_D922"
#define WIFI_PASS               "81336180"
#define MQTT_BROKER_URI         "mqtt://broker.hivemq.com:1883"
#define MQTT_CLIENT_ID          "porton_automatico_01"
#define MQTT_TOPIC_COMMAND      "porton/command"
#define MQTT_TOPIC_STATUS       "porton/status"
#define MQTT_TOPIC_ERROR        "porton/error"
#define MQTT_TOPIC_CONFIG       "porton/config"
#define MQTT_KEEPALIVE          60
