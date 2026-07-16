/*
 * =====================================================================
 *  KAKATA-433 - Control tipo gamepad con ESP32-S3 (ESP-IDF puro)
 * =====================================================================
 *  2 joysticks analogicos, 4 botones de frente, 4 botones gatillo,
 *  LCD I2C (con "pantallas" tipo grafica), MPU6050 (accel+giro),
 *  lectura de bateria, HID USB nativo (TinyUSB) y MQTT por WiFi.
 *
 *  Reescrito sin ninguna dependencia de Arduino: solo componentes
 *  nativos de ESP-IDF (driver, esp_wifi, mqtt, nvs_flash, esp_tinyusb).
 * =====================================================================
 */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_err.h"

#include "nvs_flash.h"
#include "nvs.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "mqtt_client.h"

#include "i2c_bus.h"
#include "lcd_i2c.h"
#include "mpu6050.h"
#include "usb_hid.h"

static const char *TAG = "kakata433";

// ---------- Mapa de pines (identico al original) ----------
#define PIN_JOY1_Y      1
#define PIN_JOY1_X      2
#define PIN_JOY0_BTN    3
#define PIN_JOY0_X      4
#define PIN_JOY0_Y      5
// io6/io7 = I2C (ver i2c_bus.h)
#define PIN_VBAT        8
#define PIN_BTN0        9
#define PIN_BTN2        10
#define PIN_BTN1        11
#define PIN_BTN3        12
#define PIN_MPU_INT     16
// io19/io20 = USB nativo, no se tocan
#define PIN_BTN_L4      39
#define PIN_BTN_L3      40
#define PIN_BTN_L2      41
#define PIN_BTN_L1      42
#define PIN_JOY1_BTN    46

// Canales ADC1 de cada pin (ESP32-S3: GPIOn -> ADC1_CHANNEL(n-1))
#define ADC_CH_JOY1_Y   ADC1_CHANNEL_0   // io1
#define ADC_CH_JOY1_X   ADC1_CHANNEL_1   // io2
#define ADC_CH_JOY0_X   ADC1_CHANNEL_3   // io4
#define ADC_CH_JOY0_Y   ADC1_CHANNEL_4   // io5
#define ADC_CH_VBAT     ADC1_CHANNEL_7   // io8

// ---------- Constantes ----------
#define ADC_MAX          4095
#define ADC_CENTER       2048
#define JOY_DEADZONE     120
#define GYRO_DEADZONE    300
#define AXIS_OUT_MAX     100

#define VBAT_DIVIDER     2.0f
#define VBAT_ADC_VREF    3.3f
#define VBAT_EMPTY       3.0f
#define VBAT_FULL        4.2f

#define CALIB_BTN_A_GPIO PIN_BTN_L1   // gatillo superior izquierdo
#define CALIB_BTN_B_GPIO PIN_BTN_L2   // gatillo superior derecho
#define CALIB_HOLD_MS    3000

// ---------- WiFi / MQTT ----------
#define WIFI_SSID          "TU_RED_WIFI"
#define WIFI_PASS          "TU_PASSWORD"
#define MQTT_BROKER_URI    "mqtt://192.168.1.100:1883"
#define MQTT_TOPIC_DATA    "kakata433/data"
#define MQTT_TOPIC_STATUS  "kakata433/status"
#define MQTT_PUBLISH_MS    100

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static bool s_mqtt_connected = false;

// ---------- Pantallas del LCD ----------
enum {
    SCR_ESTADO = 0,
    SCR_JOY0,
    SCR_JOY1,
    SCR_GIRO,
    SCR_ACCEL,
    SCR_BOTONES,
    SCREEN_COUNT
};

// Indices de botones en la mascara de 32 bits (1..N)
enum {
    BTN_IDX_0 = 1, BTN_IDX_1, BTN_IDX_2, BTN_IDX_3,
    BTN_IDX_L1, BTN_IDX_L2, BTN_IDX_L3, BTN_IDX_L4,
    BTN_IDX_JOY0, BTN_IDX_JOY1
};

#define CH_FULL_BLOCK 255

// ---------- Estado global ----------
static int16_t axAxis, ayAxis, azAxis, gxAxis, gyAxis, gzAxis;
static float   batteryVoltage = 0.0f;
static uint8_t batteryPercent = 0;

static int centerJoy0X = ADC_CENTER, centerJoy0Y = ADC_CENTER;
static int centerJoy1X = ADC_CENTER, centerJoy1Y = ADC_CENTER;

static bool calibActive = false;
static int64_t calibStartMs = 0;

static uint8_t currentScreen = SCR_ESTADO;
static bool lastCycleBtnState = true; // true = suelto (pull-up)

static inline int64_t millis(void) { return esp_timer_get_time() / 1000; }

// =====================================================================
//  GPIO / ADC
// =====================================================================
static void gpio_buttons_init(void) {
    const int pins[] = {
        PIN_BTN0, PIN_BTN1, PIN_BTN2, PIN_BTN3,
        PIN_BTN_L1, PIN_BTN_L2, PIN_BTN_L3, PIN_BTN_L4,
        PIN_JOY0_BTN, PIN_JOY1_BTN
    };
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << pins[i],
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&cfg);
    }

    gpio_config_t mpu_int_cfg = {
        .pin_bit_mask = 1ULL << PIN_MPU_INT,
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_DISABLE, // se lee por polling, ver mpu_int_high()
    };
    gpio_config(&mpu_int_cfg);
}

static void adc_init(void) {
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC_CH_JOY0_X, ADC_ATTEN_DB_11);
    adc1_config_channel_atten(ADC_CH_JOY0_Y, ADC_ATTEN_DB_11);
    adc1_config_channel_atten(ADC_CH_JOY1_X, ADC_ATTEN_DB_11);
    adc1_config_channel_atten(ADC_CH_JOY1_Y, ADC_ATTEN_DB_11);
    adc1_config_channel_atten(ADC_CH_VBAT, ADC_ATTEN_DB_11);
}

// ---------- Joysticks (salida -100 .. +100) ----------
static int8_t readAxis(adc1_channel_t ch, int center) {
    int raw = adc1_get_raw(ch);
    int delta = raw - center;
    if (abs(delta) < JOY_DEADZONE) delta = 0;

    int span, mapped;
    if (delta < 0) {
        span = center > 0 ? center : 1;
        mapped = (int)((int64_t)delta * AXIS_OUT_MAX / span);
    } else {
        span = (ADC_MAX - center) > 0 ? (ADC_MAX - center) : 1;
        mapped = (int)((int64_t)delta * AXIS_OUT_MAX / span);
    }
    if (mapped > AXIS_OUT_MAX) mapped = AXIS_OUT_MAX;
    if (mapped < -AXIS_OUT_MAX) mapped = -AXIS_OUT_MAX;
    return (int8_t)mapped;
}

// ---------- Giroscopio (tratado igual que un joystick, -100 .. +100) ----------
static int8_t gyroToAxis(int16_t raw) {
    if (abs((int)raw) < GYRO_DEADZONE) raw = 0;
    int mapped = (int)(((int64_t)raw * AXIS_OUT_MAX) / 32768);
    if (mapped > AXIS_OUT_MAX) mapped = AXIS_OUT_MAX;
    if (mapped < -AXIS_OUT_MAX) mapped = -AXIS_OUT_MAX;
    return (int8_t)mapped;
}

static void readBattery(void) {
    int raw = adc1_get_raw(ADC_CH_VBAT);
    float pinVoltage = (raw / (float)ADC_MAX) * VBAT_ADC_VREF;
    batteryVoltage = pinVoltage * VBAT_DIVIDER;

    float pct = (batteryVoltage - VBAT_EMPTY) / (VBAT_FULL - VBAT_EMPTY) * 100.0f;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    batteryPercent = (uint8_t)pct;
}

static uint32_t readButtonMask(void) {
    uint32_t mask = 0;
    if (gpio_get_level(PIN_BTN0)     == 0) mask |= (1UL << (BTN_IDX_0    - 1));
    if (gpio_get_level(PIN_BTN1)     == 0) mask |= (1UL << (BTN_IDX_1    - 1));
    if (gpio_get_level(PIN_BTN2)     == 0) mask |= (1UL << (BTN_IDX_2    - 1));
    if (gpio_get_level(PIN_BTN3)     == 0) mask |= (1UL << (BTN_IDX_3    - 1));
    if (gpio_get_level(PIN_BTN_L1)   == 0) mask |= (1UL << (BTN_IDX_L1   - 1));
    if (gpio_get_level(PIN_BTN_L2)   == 0) mask |= (1UL << (BTN_IDX_L2   - 1));
    if (gpio_get_level(PIN_BTN_L3)   == 0) mask |= (1UL << (BTN_IDX_L3   - 1));
    if (gpio_get_level(PIN_BTN_L4)   == 0) mask |= (1UL << (BTN_IDX_L4   - 1));
    if (gpio_get_level(PIN_JOY0_BTN) == 0) mask |= (1UL << (BTN_IDX_JOY0 - 1));
    if (gpio_get_level(PIN_JOY1_BTN) == 0) mask |= (1UL << (BTN_IDX_JOY1 - 1));
    return mask;
}

// =====================================================================
//  Calibracion del punto cero (guardada en NVS)
// =====================================================================
static void loadCalibration(void) {
    nvs_handle_t h;
    if (nvs_open("kakata433", NVS_READONLY, &h) != ESP_OK) return;
    int32_t v;
    if (nvs_get_i32(h, "c0x", &v) == ESP_OK) centerJoy0X = v;
    if (nvs_get_i32(h, "c0y", &v) == ESP_OK) centerJoy0Y = v;
    if (nvs_get_i32(h, "c1x", &v) == ESP_OK) centerJoy1X = v;
    if (nvs_get_i32(h, "c1y", &v) == ESP_OK) centerJoy1Y = v;
    nvs_close(h);
}

static void saveCalibration(void) {
    nvs_handle_t h;
    if (nvs_open("kakata433", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, "c0x", centerJoy0X);
    nvs_set_i32(h, "c0y", centerJoy0Y);
    nvs_set_i32(h, "c1x", centerJoy1X);
    nvs_set_i32(h, "c1y", centerJoy1Y);
    nvs_commit(h);
    nvs_close(h);
}

// Mantener L1+L2 presionados 3s para recalibrar el centro de los joysticks
static bool handleCalibration(void) {
    bool bothPressed = (gpio_get_level(CALIB_BTN_A_GPIO) == 0) &&
                        (gpio_get_level(CALIB_BTN_B_GPIO) == 0);

    if (!bothPressed) {
        calibActive = false;
        return false;
    }

    if (!calibActive) {
        calibActive = true;
        calibStartMs = millis();
    }

    int64_t held = millis() - calibStartMs;

    lcd_set_cursor(0, 0);
    lcd_print("Calibrando...   ");
    lcd_set_cursor(0, 1);
    int barLen = (int)(held * LCD_COLS / CALIB_HOLD_MS);
    if (barLen > LCD_COLS) barLen = LCD_COLS;
    for (int i = 0; i < LCD_COLS; i++) {
        lcd_print_char(i < barLen ? (char)CH_FULL_BLOCK : ' ');
    }

    if (held >= CALIB_HOLD_MS) {
        const int N = 20;
        long sx0 = 0, sy0 = 0, sx1 = 0, sy1 = 0;
        for (int i = 0; i < N; i++) {
            sx0 += adc1_get_raw(ADC_CH_JOY0_X);
            sy0 += adc1_get_raw(ADC_CH_JOY0_Y);
            sx1 += adc1_get_raw(ADC_CH_JOY1_X);
            sy1 += adc1_get_raw(ADC_CH_JOY1_Y);
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        centerJoy0X = sx0 / N;
        centerJoy0Y = sy0 / N;
        centerJoy1X = sx1 / N;
        centerJoy1Y = sy1 / N;
        saveCalibration();

        lcd_clear();
        lcd_set_cursor(0, 0);
        lcd_print("Calibracion");
        lcd_set_cursor(0, 1);
        lcd_print("completada!");
        vTaskDelay(pdMS_TO_TICKS(1200));
        lcd_clear();
        calibActive = false;
    }
    return true;
}

// =====================================================================
//  WiFi
// =====================================================================
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void) {
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                &wifi_event_handler, NULL));

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_print("Conectando WiFi");
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE,
                        pdMS_TO_TICKS(15000));

    lcd_clear();
    lcd_set_cursor(0, 0);
    if (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT) {
        lcd_print("WiFi OK");
    } else {
        lcd_print("WiFi no conecto");
    }
    vTaskDelay(pdMS_TO_TICKS(1200));
    lcd_clear();
}

// =====================================================================
//  MQTT
// =====================================================================
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data) {
    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            s_mqtt_connected = true;
            esp_mqtt_client_publish(s_mqtt_client, MQTT_TOPIC_STATUS, "online", 0, 0, 1);
            break;
        case MQTT_EVENT_DISCONNECTED:
            s_mqtt_connected = false;
            break;
        default:
            break;
    }
}

static void mqtt_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
    };
    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt_client);
}

// Publica un JSON con todo el estado, para verlo desde el celular
// (por ejemplo con "IoT MQTT Panel" suscrito a kakata433/data)
static void publishGamepadData(int8_t jx0, int8_t jy0, int8_t jx1, int8_t jy1,
                                int8_t gx, int8_t gy, int8_t gz,
                                float axG, float ayG, float azG,
                                uint32_t buttons) {
    if (!s_mqtt_connected) return;

    char payload[256];
    snprintf(payload, sizeof(payload),
        "{\"j0\":{\"x\":%d,\"y\":%d},"
        "\"j1\":{\"x\":%d,\"y\":%d},"
        "\"gyro\":{\"x\":%d,\"y\":%d,\"z\":%d},"
        "\"accel\":{\"x\":%.2f,\"y\":%.2f,\"z\":%.2f},"
        "\"bat\":%u,\"btn\":%lu}",
        jx0, jy0, jx1, jy1, gx, gy, gz, axG, ayG, azG,
        batteryPercent, (unsigned long)buttons);

    esp_mqtt_client_publish(s_mqtt_client, MQTT_TOPIC_DATA, payload, 0, 0, 0);
}

// =====================================================================
//  Pantallas del LCD
// =====================================================================
static void drawCenteredBar(int row, int col, int8_t value) {
    int pos = ((int)value + AXIS_OUT_MAX) * 10 / (2 * AXIS_OUT_MAX);
    lcd_set_cursor(col, row);
    for (int i = 0; i <= 10; i++) {
        if (i == pos)    lcd_print_char((char)CH_FULL_BLOCK);
        else if (i == 5) lcd_print_char('|');
        else             lcd_print_char('-');
    }
}

static void printPadded(int value, int width) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%*d", width, value);
    lcd_print(buf);
}

static void screenEstado(void) {
    lcd_set_cursor(0, 0);
    lcd_print("Bat:");
    printPadded(batteryPercent, 3);
    lcd_print("% ");
    char v[6]; snprintf(v, sizeof(v), "%.1f", batteryVoltage);
    lcd_print(v);
    lcd_print("V");

    lcd_set_cursor(0, 1);
    lcd_print("WiFi:");
    lcd_print((xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT) ? "OK " : "-- ");
    lcd_print("MQTT:");
    lcd_print(s_mqtt_connected ? "OK" : "--");
}

static void screenJoy0(int8_t jx0, int8_t jy0) {
    lcd_set_cursor(0, 0); lcd_print("X"); printPadded(jx0, 4);
    drawCenteredBar(0, 5, jx0);
    lcd_set_cursor(0, 1); lcd_print("Y"); printPadded(jy0, 4);
    drawCenteredBar(1, 5, jy0);
}

static void screenJoy1(int8_t jx1, int8_t jy1) {
    lcd_set_cursor(0, 0); lcd_print("x"); printPadded(jx1, 4);
    drawCenteredBar(0, 5, jx1);
    lcd_set_cursor(0, 1); lcd_print("y"); printPadded(jy1, 4);
    drawCenteredBar(1, 5, jy1);
}

static void screenGiro(int8_t gx, int8_t gy, int8_t gz) {
    lcd_set_cursor(0, 0);
    lcd_print("Gx"); printPadded(gx, 4); lcd_print(" Gy"); printPadded(gy, 4);
    lcd_set_cursor(0, 1);
    lcd_print("Gz"); printPadded(gz, 4); lcd_print("      ");
}

static void screenAccel(float axG, float ayG, float azG) {
    char bx[6], by[6], bz[6];
    snprintf(bx, sizeof(bx), "%.2f", axG);
    snprintf(by, sizeof(by), "%.2f", ayG);
    snprintf(bz, sizeof(bz), "%.2f", azG);

    lcd_set_cursor(0, 0);
    lcd_print("Ax"); lcd_print(bx); lcd_print(" Ay"); lcd_print(by);
    lcd_set_cursor(0, 1);
    lcd_print("Az"); lcd_print(bz); lcd_print("g      ");
}

static void screenBotones(uint32_t buttons) {
    lcd_set_cursor(0, 0);
    lcd_print("Frnt:");
    lcd_print_char((buttons & (1UL << (BTN_IDX_0 - 1))) ? '1' : '0');
    lcd_print_char((buttons & (1UL << (BTN_IDX_1 - 1))) ? '1' : '0');
    lcd_print_char((buttons & (1UL << (BTN_IDX_2 - 1))) ? '1' : '0');
    lcd_print_char((buttons & (1UL << (BTN_IDX_3 - 1))) ? '1' : '0');
    lcd_print("        ");

    lcd_set_cursor(0, 1);
    lcd_print("Gat: ");
    lcd_print_char((buttons & (1UL << (BTN_IDX_L1 - 1))) ? '1' : '0');
    lcd_print_char((buttons & (1UL << (BTN_IDX_L2 - 1))) ? '1' : '0');
    lcd_print_char((buttons & (1UL << (BTN_IDX_L3 - 1))) ? '1' : '0');
    lcd_print_char((buttons & (1UL << (BTN_IDX_L4 - 1))) ? '1' : '0');
    lcd_print("        ");
}

static void checkScreenCycle(void) {
    bool state = gpio_get_level(PIN_BTN3);
    if (!state && lastCycleBtnState) {
        currentScreen = (currentScreen + 1) % SCREEN_COUNT;
        lcd_clear();
    }
    lastCycleBtnState = state;
}

static void updateLCD(int8_t jx0, int8_t jy0, int8_t jx1, int8_t jy1,
                       int8_t gx, int8_t gy, int8_t gz,
                       float axG, float ayG, float azG, uint32_t buttons) {
    switch (currentScreen) {
        case SCR_ESTADO:  screenEstado(); break;
        case SCR_JOY0:    screenJoy0(jx0, jy0); break;
        case SCR_JOY1:    screenJoy1(jx1, jy1); break;
        case SCR_GIRO:    screenGiro(gx, gy, gz); break;
        case SCR_ACCEL:   screenAccel(axG, ayG, azG); break;
        case SCR_BOTONES: screenBotones(buttons); break;
    }
}

// =====================================================================
//  app_main
// =====================================================================
void app_main(void) {
    ESP_LOGI(TAG, "KAKATA-433 iniciando...");

    // NVS: necesario tanto para la calibracion como para WiFi
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    gpio_buttons_init();
    adc_init();

    i2c_bus_init();
    lcd_init();
    lcd_set_cursor(0, 0);
    lcd_print("KAKATA-433");
    lcd_set_cursor(0, 1);
    lcd_print("Iniciando...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    lcd_clear();

    mpu6050_init();
    loadCalibration();

    usb_hid_init();

    wifi_init_sta();
    mqtt_start();

    int64_t lastSend = 0, lastLCD = 0, lastBattery = 0, lastMqtt = 0;

    while (1) {
        int64_t now = millis();

        if (now - lastBattery >= 1000) {
            lastBattery = now;
            readBattery();
        }

        // Lee el MPU6050 (sin usar interrupcion, por simplicidad se
        // consulta directamente en cada vuelta)
        mpu6050_read(&axAxis, &ayAxis, &azAxis, &gxAxis, &gyAxis, &gzAxis);

        bool calibrando = handleCalibration();
        if (!calibrando) {
            checkScreenCycle();
        }

        int8_t jx0 = readAxis(ADC_CH_JOY0_X, centerJoy0X);
        int8_t jy0 = readAxis(ADC_CH_JOY0_Y, centerJoy0Y);
        int8_t jx1 = readAxis(ADC_CH_JOY1_X, centerJoy1X);
        int8_t jy1 = readAxis(ADC_CH_JOY1_Y, centerJoy1Y);

        int8_t gx = gyroToAxis(gxAxis);
        int8_t gy = gyroToAxis(gyAxis);
        int8_t gz = gyroToAxis(gzAxis);

        // Sensibilidad del MPU6050 en +-2g: 16384 LSB/g
        float axG = axAxis / 16384.0f;
        float ayG = ayAxis / 16384.0f;
        float azG = azAxis / 16384.0f;

        uint32_t buttons = readButtonMask();

        if (now - lastSend >= 15) { // ~66 Hz
            lastSend = now;
            usb_hid_send(jx0, jy0, jx1, jy1, buttons);
        }

        if (now - lastMqtt >= MQTT_PUBLISH_MS) {
            lastMqtt = now;
            publishGamepadData(jx0, jy0, jx1, jy1, gx, gy, gz, axG, ayG, azG, buttons);
        }

        if (!calibrando && (now - lastLCD >= 250)) {
            lastLCD = now;
            updateLCD(jx0, jy0, jx1, jy1, gx, gy, gz, axG, ayG, azG, buttons);
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
