#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "driver/gpio.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"

#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "mqtt_client.h"


// ============================================================
// CONFIGURACIÓN DE GPIO
// ============================================================

#define LED_GPIO        GPIO_NUM_2

#define PB1_GPIO        GPIO_NUM_4
#define PB2_GPIO        GPIO_NUM_5


// ============================================================
// CONFIGURACIÓN WIFI
// ============================================================

#define WIFI_SSID       "TP-Link_D922"
#define WIFI_PASSWORD   "81336180"


// ============================================================
// CONFIGURACIÓN MQTT
// ============================================================

#define MQTT_BROKER     "mqtt://broker.hivemq.com"

#define MQTT_TOPIC_REACTION \
    "itla/reaccion/tiempo_reaccion"

#define MQTT_TOPIC_PB2 \
    "itla/reaccion/tiempo_pb2"

#define MQTT_TOPIC_TOTAL \
    "itla/reaccion/tiempo_total"


// ============================================================
// TIEMPO ALEATORIO
// ============================================================

#define RANDOM_MIN_MS   2000
#define RANDOM_MAX_MS   5000


// ============================================================
// ANTIRREBOTE
// ============================================================

#define DEBOUNCE_TIME_US 50000


// ============================================================
// TAG
// ============================================================

static const char *TAG = "REACTION";


// ============================================================
// ESTADOS DEL SISTEMA
// ============================================================

typedef enum
{
    STATE_WAIT_PB1,
    STATE_RANDOM_WAIT,
    STATE_LED_ON,
    STATE_WAIT_PB2,
    STATE_RESULT

} reaction_state_t;


static volatile reaction_state_t state =
    STATE_WAIT_PB1;


// ============================================================
// VARIABLES DE TIEMPO
// ============================================================

static volatile int64_t led_time_us = 0;

static volatile int64_t pb1_release_time_us = 0;

static volatile int64_t pb2_press_time_us = 0;


// ============================================================
// ANTIRREBOTE
// ============================================================

static volatile int64_t last_pb1_interrupt_us = 0;

static volatile int64_t last_pb2_interrupt_us = 0;


// ============================================================
// MQTT
// ============================================================

static esp_mqtt_client_handle_t mqtt_client = NULL;

static volatile bool mqtt_connected = false;


// ============================================================
// WIFI
// ============================================================

static EventGroupHandle_t wifi_event_group;

#define WIFI_CONNECTED_BIT BIT0


// ============================================================
// ISR DE LOS BOTONES
// ============================================================

static void IRAM_ATTR button_isr_handler(void *arg)
{
    gpio_num_t gpio_num =
        (gpio_num_t)(uintptr_t)arg;

    int level =
        gpio_get_level(gpio_num);

    int64_t now =
        esp_timer_get_time();


    // ========================================================
    // PB1
    // ========================================================

    if (gpio_num == PB1_GPIO)
    {
        // Antirrebote PB1

        if ((now - last_pb1_interrupt_us)
            < DEBOUNCE_TIME_US)
        {
            return;
        }

        last_pb1_interrupt_us = now;


        // ----------------------------------------------------
        // PB1 PRESIONADO
        // ----------------------------------------------------

        if (level == 1)
        {
            if (state == STATE_WAIT_PB1)
            {
                state = STATE_RANDOM_WAIT;

                ESP_EARLY_LOGI(
                    TAG,
                    "PB1 presionado"
                );
            }
        }


        // ----------------------------------------------------
        // PB1 SOLTADO
        // ----------------------------------------------------

        else
        {
            if (state == STATE_LED_ON)
            {
                pb1_release_time_us = now;

                state = STATE_WAIT_PB2;

                ESP_EARLY_LOGI(
                    TAG,
                    "PB1 soltado"
                );
            }
        }
    }


    // ========================================================
    // PB2
    // ========================================================

    if (gpio_num == PB2_GPIO)
    {
        // Antirrebote PB2

        if ((now - last_pb2_interrupt_us)
            < DEBOUNCE_TIME_US)
        {
            return;
        }

        last_pb2_interrupt_us = now;


        // ----------------------------------------------------
        // PB2 PRESIONADO
        // ----------------------------------------------------

        if (level == 1)
        {
            if (state == STATE_WAIT_PB2)
            {
                pb2_press_time_us = now;

                state = STATE_RESULT;

                ESP_EARLY_LOGI(
                    TAG,
                    "PB2 presionado"
                );
            }
        }
    }
}


// ============================================================
// CONFIGURACIÓN GPIO
// ============================================================

static void gpio_init_all(void)
{
    // ========================================================
    // LED
    // ========================================================

    gpio_config_t led_config = {
        .pin_bit_mask =
            (1ULL << LED_GPIO),

        .mode =
            GPIO_MODE_OUTPUT,

        .pull_up_en =
            GPIO_PULLUP_DISABLE,

        .pull_down_en =
            GPIO_PULLDOWN_DISABLE,

        .intr_type =
            GPIO_INTR_DISABLE
    };


    ESP_ERROR_CHECK(
        gpio_config(&led_config)
    );


    // LED apagado

    gpio_set_level(
        LED_GPIO,
        0
    );


    // ========================================================
    // BOTONES
    // ========================================================

    gpio_config_t button_config = {
        .pin_bit_mask =
            (1ULL << PB1_GPIO) |
            (1ULL << PB2_GPIO),

        .mode =
            GPIO_MODE_INPUT,

        // NO usamos pull-up

        .pull_up_en =
            GPIO_PULLUP_DISABLE,

        // Usamos pull-down

        .pull_down_en =
            GPIO_PULLDOWN_ENABLE,

        // Detectar subida y bajada

        .intr_type =
            GPIO_INTR_ANYEDGE
    };


    ESP_ERROR_CHECK(
        gpio_config(&button_config)
    );


    // ========================================================
    // INSTALAR SERVICIO DE INTERRUPCIONES
    // ========================================================

    ESP_ERROR_CHECK(
        gpio_install_isr_service(0)
    );


    // PB1

    ESP_ERROR_CHECK(
        gpio_isr_handler_add(
            PB1_GPIO,
            button_isr_handler,
            (void *)(uintptr_t)PB1_GPIO
        )
    );


    // PB2

    ESP_ERROR_CHECK(
        gpio_isr_handler_add(
            PB2_GPIO,
            button_isr_handler,
            (void *)(uintptr_t)PB2_GPIO
        )
    );


    ESP_LOGI(
        TAG,
        "GPIO configurados"
    );
}


// ============================================================
// WIFI EVENT HANDLER
// ============================================================

static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    // ========================================================
    // WIFI INICIADO
    // ========================================================

    if (
        event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_START
    )
    {
        ESP_LOGI(
            TAG,
            "Conectando al WiFi..."
        );

        esp_wifi_connect();
    }


    // ========================================================
    // WIFI DESCONECTADO
    // ========================================================

    else if (
        event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_DISCONNECTED
    )
    {
        ESP_LOGW(
            TAG,
            "WiFi desconectado"
        );

        esp_wifi_connect();
    }


    // ========================================================
    // IP OBTENIDA
    // ========================================================

    else if (
        event_base == IP_EVENT &&
        event_id == IP_EVENT_STA_GOT_IP
    )
    {
        ESP_LOGI(
            TAG,
            "WiFi conectado"
        );

        xEventGroupSetBits(
            wifi_event_group,
            WIFI_CONNECTED_BIT
        );
    }
}


// ============================================================
// INICIALIZAR WIFI
// ============================================================

static void wifi_init(void)
{
    wifi_event_group =
        xEventGroupCreate();


    ESP_ERROR_CHECK(
        esp_netif_init()
    );


    ESP_ERROR_CHECK(
        esp_event_loop_create_default()
    );


    esp_netif_create_default_wifi_sta();


    wifi_init_config_t cfg =
        WIFI_INIT_CONFIG_DEFAULT();


    ESP_ERROR_CHECK(
        esp_wifi_init(&cfg)
    );


    // ========================================================
    // EVENTOS WIFI
    // ========================================================

    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &wifi_event_handler,
            NULL,
            NULL
        )
    );


    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &wifi_event_handler,
            NULL,
            NULL
        )
    );


    // ========================================================
    // CONFIGURACIÓN
    // ========================================================

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,

            .threshold.authmode =
                WIFI_AUTH_WPA2_PSK
        }
    };


    ESP_ERROR_CHECK(
        esp_wifi_set_mode(
            WIFI_MODE_STA
        )
    );


    ESP_ERROR_CHECK(
        esp_wifi_set_config(
            WIFI_IF_STA,
            &wifi_config
        )
    );


    ESP_ERROR_CHECK(
        esp_wifi_start()
    );


    // Esperar conexión

    xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY
    );
}


// ============================================================
// MQTT EVENT HANDLER
// ============================================================

static void mqtt_event_handler(
    void *handler_args,
    esp_event_base_t base,
    int32_t event_id,
    void *event_data)
{
    esp_mqtt_event_handle_t event =
        event_data;


    switch (
        (esp_mqtt_event_id_t)event_id
    )
    {
        case MQTT_EVENT_CONNECTED:

            ESP_LOGI(
                TAG,
                "MQTT conectado"
            );

            mqtt_connected = true;

            break;


        case MQTT_EVENT_DISCONNECTED:

            ESP_LOGW(
                TAG,
                "MQTT desconectado"
            );

            mqtt_connected = false;

            break;


        default:

            break;
    }
}


// ============================================================
// INICIALIZAR MQTT
// ============================================================

static void mqtt_init(void)
{
    const esp_mqtt_client_config_t mqtt_cfg = {

        .broker.address.uri =
            MQTT_BROKER
    };


    mqtt_client =
        esp_mqtt_client_init(
            &mqtt_cfg
        );


    ESP_ERROR_CHECK(
        esp_mqtt_client_register_event(
            mqtt_client,
            ESP_EVENT_ANY_ID,
            mqtt_event_handler,
            NULL
        )
    );


    ESP_ERROR_CHECK(
        esp_mqtt_client_start(
            mqtt_client
        )
    );
}


// ============================================================
// PUBLICAR RESULTADOS
// ============================================================

static void publish_results(void)
{
    if (!mqtt_connected)
    {
        ESP_LOGW(
            TAG,
            "MQTT no conectado"
        );

        return;
    }



    // ========================================================
    // CALCULAR TIEMPOS
    // ========================================================

    int64_t reaction_us =
        pb1_release_time_us -
        led_time_us;

    int64_t pb2_us =
        pb2_press_time_us -
        pb1_release_time_us;

    int64_t total_us =
        pb2_press_time_us -
        led_time_us;



    // ========================================================
    // CONVERTIR A MILISEGUNDOS
    // ========================================================

    double reaction_s =
        reaction_us / 1000000.0;

    double pb2_s =
        pb2_us / 1000000.0;

    double total_s =
        total_us / 1000000.0;



    // ========================================================
    // MOSTRAR EN MONITOR SERIAL
    // ========================================================

    ESP_LOGI(
        TAG,
        "================================"
    );

    ESP_LOGI(
        TAG,
        "RESULTADOS"
    );

    ESP_LOGI(
        TAG,
        "Tiempo de reaccion: %.3f s",
        reaction_s
    );

    ESP_LOGI(
        TAG,
        "Tiempo PB2: %.3f s",
        pb2_s
    );

    ESP_LOGI(
        TAG,
        "Tiempo total: %.3f s",
        total_s
    );

    ESP_LOGI(
        TAG,
        "================================"
    );


    // ========================================================
    // CONVERTIR A TEXTO
    // ========================================================

    char reaction_data[32];
    char pb2_data[32];
    char total_data[32];


    snprintf(
        reaction_data,
        sizeof(reaction_data),
        "%.3f s",
        reaction_s
    );

    snprintf(
        pb2_data,
        sizeof(pb2_data),
        "%.3f s",
        pb2_s
    );

    snprintf(
        total_data,
        sizeof(total_data),
        "%.3f s",
        total_s
    );

    // ========================================================
    // PUBLICAR MQTT
    // ========================================================

     esp_mqtt_client_publish(
        mqtt_client,
        MQTT_TOPIC_REACTION,
        reaction_data,
        0,
        1,
        0
    );


    esp_mqtt_client_publish(
        mqtt_client,
        MQTT_TOPIC_PB2,
        pb2_data,
        0,
        1,
        0
    );


    esp_mqtt_client_publish(
        mqtt_client,
        MQTT_TOPIC_TOTAL,
        total_data,
        0,
        1,
        0
    );
}

// ============================================================
// TAREA DEL SISTEMA
// ============================================================

static void reaction_task(void *pvParameter)
{
    while (1)
    {
        switch (state)
        {
            // =================================================
            // ESPERAR QUE EL USUARIO PRESIONE PB1
            // =================================================

            case STATE_WAIT_PB1:

                gpio_set_level(
                    LED_GPIO,
                    0
                );

                break;


            // =================================================
            // PB1 PRESIONADO
            // ESPERAR TIEMPO ALEATORIO
            // =================================================

            case STATE_RANDOM_WAIT:
            {
                // Generar número aleatorio

                uint32_t random_ms =
                    RANDOM_MIN_MS +
                    (
                        esp_random() %
                        (
                            RANDOM_MAX_MS -
                            RANDOM_MIN_MS +
                            1
                        )
                    );


                ESP_LOGI(
                    TAG,
                    "PB1 presionado"
                );


                ESP_LOGI(
                    TAG,
                    "Esperando %" PRIu32 " ms...",
                    random_ms
                );


                // Esperar

                vTaskDelay(
                    pdMS_TO_TICKS(random_ms)
                );


                // =================================================
                // COMPROBAR QUE EL USUARIO SIGA PRESIONANDO PB1
                // =================================================

                if (
                    gpio_get_level(PB1_GPIO) == 1
                )
                {
                    // Encender LED

                    gpio_set_level(
                        LED_GPIO,
                        1
                    );


                    // Registrar instante exacto

                    led_time_us =
                        esp_timer_get_time();


                    ESP_LOGI(
                        TAG,
                        "LED ENCENDIDO"
                    );


                    state =
                        STATE_LED_ON;
                }

                else
                {
                    // El usuario soltó PB1 antes
                    // de que apareciera la señal.

                    ESP_LOGI(
                        TAG,
                        "PB1 fue soltado antes de la señal"
                    );


                    state =
                        STATE_WAIT_PB1;
                }

                break;
            }


            // =================================================
            // LED ENCENDIDO
            // =================================================

            case STATE_LED_ON:

                /*
                 * Aquí esperamos que el usuario
                 * suelte PB1.
                 *
                 * El ISR se encarga de detectarlo.
                 */

                break;


            // =================================================
            // ESPERAR PB2
            // =================================================

            case STATE_WAIT_PB2:

                /*
                 * Aquí esperamos que el usuario
                 * presione PB2.
                 *
                 * El ISR se encarga de detectarlo.
                 */

                break;


            // =================================================
            // RESULTADO
            // =================================================

            case STATE_RESULT:

                // Apagar LED

                gpio_set_level(
                    LED_GPIO,
                    0
                );


                // Enviar resultados

                publish_results();


                // Pequeña pausa

                vTaskDelay(
                    pdMS_TO_TICKS(1000)
                );


                // Nueva prueba

                state =
                    STATE_WAIT_PB1;

                break;
        }


        /*
         * Pequeña pausa para evitar que la tarea
         * consuma completamente la CPU.
         */

        vTaskDelay(
            pdMS_TO_TICKS(1)
        );
    }
}


// ============================================================
// APP MAIN
// ============================================================

void app_main(void)
{
    ESP_LOGI(
        TAG,
        "================================"
    );

    ESP_LOGI(
        TAG,
        "SISTEMA DE REACCION HUMANA"
    );

    ESP_LOGI(
        TAG,
        "ESP32-S3"
    );

    ESP_LOGI(
        TAG,
        "================================"
    );


    // ========================================================
    // INICIALIZAR NVS
    // ========================================================

    esp_err_t ret =
        nvs_flash_init();


    if (
        ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND
    )
    {
        ESP_ERROR_CHECK(
            nvs_flash_erase()
        );


        ret =
            nvs_flash_init();
    }


    ESP_ERROR_CHECK(ret);


    // ========================================================
    // GPIO
    // ========================================================

    gpio_init_all();


    // ========================================================
    // WIFI
    // ========================================================

    wifi_init();


    // ========================================================
    // MQTT
    // ========================================================

    mqtt_init();


    // ========================================================
    // TAREA PRINCIPAL
    // ========================================================

    xTaskCreate(
        reaction_task,
        "reaction_task",
        4096,
        NULL,
        10,
        NULL
    );
}
