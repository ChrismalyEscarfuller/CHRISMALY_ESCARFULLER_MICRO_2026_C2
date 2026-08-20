/*
 * ============================================================
 * JUEGO DE AGILIDAD
 * ESP32 + ESP-IDF + MQTT
 * ============================================================
 *
 * FLUJO:
 *
 * MENU
 *  ├── JUEGO1
 *  └── JUEGO2
 *
 * Para cambiar de juego:
 *
 * JUEGO1 -> MENU -> JUEGO2
 *
 * JUEGO2 -> MENU -> JUEGO1
 *
 *
 * ============================================================
 * TOPIC DE COMANDOS
 * ============================================================
 *
 * juego/agilidad/comando
 *
 * Comandos:
 *
 * JUEGO1
 * JUEGO2
 * MENU
 * RESET1
 * RESET2
 * BOTON1
 * SOLTAR1
 * BOTON2
 *
 *
 * ============================================================
 * TOPICS DE SALIDA
 * ============================================================
 *
 * juego/agilidad/estado
 *
 * juego/agilidad/juego1/reaccion1
 * juego/agilidad/juego1/reaccion2
 * juego/agilidad/juego1/promedio
 * juego/agilidad/juego1/falsa_salida
 *
 * juego/agilidad/juego2/resultado
 *
 * ============================================================
 *
 * JUEGO 1
 *
 * Reaccion 1:
 * LED encendido -> soltar B1
 *
 * Reaccion 2:
 * soltar B1 -> presionar B2
 *
 * Promedio:
 * promedio de las 10 REACCIONES 2
 *
 *
 * JUEGO 2
 *
 * Durante 1 segundo:
 *
 * B1 -> B2 -> B1 -> B2...
 *
 * Cada cambio valido cuenta como un paso.
 *
 * ============================================================
 */


#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"

#include "nvs_flash.h"

#include "mqtt_client.h"

#include "esp_random.h"
#include "esp_timer.h"


// ============================================================
// WIFI
// ============================================================

#define WIFI_SSID       "Docentes_Administrativos"
#define WIFI_PASSWORD   "Adm1N2584km"


// ============================================================
// MQTT
// ============================================================

#define MQTT_BROKER_URI "mqtt://test.mosquitto.org:1883"


// ============================================================
// TOPICS
// ============================================================

// -------------------- COMANDO -------------------------------

#define TOPIC_COMANDO \
    "juego/agilidad/comando"


// -------------------- ESTADO -------------------------------

#define TOPIC_ESTADO \
    "juego/agilidad/estado"


// -------------------- JUEGO 1 -------------------------------

#define TOPIC_J1_REACCION1 \
    "juego/agilidad/juego1/reaccion1"

#define TOPIC_J1_REACCION2 \
    "juego/agilidad/juego1/reaccion2"

#define TOPIC_J1_PROMEDIO \
    "juego/agilidad/juego1/promedio"

#define TOPIC_J1_FALSA_SALIDA \
    "juego/agilidad/juego1/falsa_salida"


// -------------------- JUEGO 2 -------------------------------

#define TOPIC_J2_RESULTADO \
    "juego/agilidad/juego2/resultado"


// ============================================================
// PINES
// ============================================================

#define PIN_BOTON1 GPIO_NUM_18
#define PIN_BOTON2 GPIO_NUM_19

#define PIN_LED    GPIO_NUM_23


// ============================================================
// BOTONES
// ============================================================

#define PRESIONADO 0

#define SUELTO 1


// ============================================================
// PROMEDIO
// ============================================================

#define NUM_JUGADAS_PROMEDIO 10


// ============================================================
// TAG
// ============================================================

static const char *TAG = "JUEGO";


// ============================================================
// MQTT
// ============================================================

static esp_mqtt_client_handle_t mqtt_client = NULL;

static bool mqtt_conectado = false;


// ============================================================
// MODOS
// ============================================================

typedef enum
{
    MODO_MENU,

    MODO_JUEGO1,

    MODO_JUEGO2

} modo_juego_t;


static modo_juego_t modo = MODO_MENU;


// ============================================================
// ESTADOS
// ============================================================

typedef enum
{
    ESPERANDO_INICIO,

    ESPERANDO_LED,

    ESPERANDO_ACCION,

    JUEGO_TERMINADO

} estado_juego_t;


static estado_juego_t estado = ESPERANDO_INICIO;


// ============================================================
// TIEMPOS JUEGO 1
// ============================================================

static int64_t tiempo_inicio_espera = 0;

static int64_t tiempo_led_encendido = 0;

static int64_t tiempo_suelta_boton1 = 0;

static int64_t delay_random = 0;


// ============================================================
// ESTADO BOTON 1 JUEGO 1
// ============================================================

static bool boton1_suelto = false;


// ============================================================
// REACCION 2
// ============================================================
//
// IMPORTANTE:
//
// El promedio es del tiempo REACCION 2.
//
// soltar B1 -> presionar B2
//
// ============================================================

static float tiempos_reaccion2[NUM_JUGADAS_PROMEDIO];

static int cantidad_reacciones2 = 0;

static int indice_reaccion2 = 0;


// ============================================================
// JUEGO 2
// ============================================================

static bool juego2_activo = false;

static int pasos_juego2 = 0;

static int ultimo_boton = 1;

static int64_t tiempo_inicio_juego2 = 0;


// ============================================================
// ESTADO ANTERIOR DE BOTONES FISICOS
// ============================================================

static int boton1_anterior = SUELTO;

static int boton2_anterior = SUELTO;


// ============================================================
// PROTOTIPOS
// ============================================================

static void mqtt_app_start(void);

static void mqtt_publicar(
    const char *topic,
    const char *mensaje
);

static void entrar_juego1(void);

static void entrar_juego2(void);

static void iniciar_juego1(void);

static void iniciar_reaccion(void);

static void reaccion_boton1_suelto(void);

static void reaccion_boton2(void);

static void falsa_salida(void);

static void iniciar_juego2(void);

static void juego2_boton(int boton);

static void finalizar_juego2(void);

static void reset_juego1(void);

static void reset_juego2(void);

static void volver_menu(void);

static void led_encender(void);

static void led_apagar(void);


// ============================================================
// MQTT PUBLICAR
// ============================================================

static void mqtt_publicar(
    const char *topic,
    const char *mensaje
)
{
    if (
        mqtt_conectado &&
        mqtt_client != NULL
    )
    {
        ESP_LOGI(
            TAG,
            "MQTT -> %s : %s",
            topic,
            mensaje
        );

        esp_mqtt_client_publish(
            mqtt_client,
            topic,
            mensaje,
            0,
            1,
            1
        );
    }
}


// ============================================================
// LED
// ============================================================

static void led_encender(void)
{
    gpio_set_level(
        PIN_LED,
        1
    );

    ESP_LOGI(
        TAG,
        "LED -> ON"
    );
}


static void led_apagar(void)
{
    gpio_set_level(
        PIN_LED,
        0
    );

    ESP_LOGI(
        TAG,
        "LED -> OFF"
    );
}


// ============================================================
// ENTRAR JUEGO 1
// ============================================================
//
// IMPORTANTE:
//
// Solo se permite entrar a JUEGO1 desde MENU.
//
// ============================================================

static void entrar_juego1(void)
{
    if (
        modo != MODO_MENU
    )
    {
        ESP_LOGW(
            TAG,
            "No se puede entrar JUEGO1 sin pasar por MENU"
        );

        return;
    }


    modo = MODO_JUEGO1;

    estado = ESPERANDO_INICIO;

    boton1_suelto = false;

    led_apagar();


    mqtt_publicar(
        TOPIC_ESTADO,
        "JUEGO1"
    );


    ESP_LOGI(
        TAG,
        "MODO JUEGO 1"
    );
}


// ============================================================
// ENTRAR JUEGO 2
// ============================================================
//
// Solo se permite entrar a JUEGO2 desde MENU.
//
// ============================================================

static void entrar_juego2(void)
{
    if (
        modo != MODO_MENU
    )
    {
        ESP_LOGW(
            TAG,
            "No se puede entrar JUEGO2 sin pasar por MENU"
        );

        return;
    }


    modo = MODO_JUEGO2;

    estado = ESPERANDO_INICIO;

    juego2_activo = false;

    pasos_juego2 = 0;

    ultimo_boton = 1;

    led_apagar();


    mqtt_publicar(
        TOPIC_ESTADO,
        "JUEGO2"
    );


    ESP_LOGI(
        TAG,
        "MODO JUEGO 2"
    );
}


// ============================================================
// VOLVER MENU
// ============================================================
//
// Desde cualquier juego se puede volver a MENU.
//
// Luego hay que enviar JUEGO1 o JUEGO2.
//
// ============================================================

static void volver_menu(void)
{
    modo = MODO_MENU;

    estado = ESPERANDO_INICIO;

    juego2_activo = false;

    boton1_suelto = false;

    pasos_juego2 = 0;

    ultimo_boton = 1;

    led_apagar();


    mqtt_publicar(
        TOPIC_ESTADO,
        "MENU"
    );


    ESP_LOGI(
        TAG,
        "MENU"
    );
}


// ============================================================
// RESET JUEGO 1
// ============================================================

static void reset_juego1(void)
{
    /*
     * RESET1 solamente funciona
     * estando dentro de JUEGO1.
     */

    if (
        modo != MODO_JUEGO1
    )
    {
        return;
    }


    estado = ESPERANDO_INICIO;

    boton1_suelto = false;


    // --------------------------------------------------------
    // Reiniciar promedio
    // --------------------------------------------------------

    cantidad_reacciones2 = 0;

    indice_reaccion2 = 0;


    for (
        int i = 0;
        i < NUM_JUGADAS_PROMEDIO;
        i++
    )
    {
        tiempos_reaccion2[i] = 0.0f;
    }


    led_apagar();


    // --------------------------------------------------------
    // Limpiar resultados
    // --------------------------------------------------------

    mqtt_publicar(
        TOPIC_J1_REACCION1,
        "0"
    );

    mqtt_publicar(
        TOPIC_J1_REACCION2,
        "0"
    );

    mqtt_publicar(
        TOPIC_J1_PROMEDIO,
        "0"
    );

    mqtt_publicar(
        TOPIC_J1_FALSA_SALIDA,
        "0"
    );


    mqtt_publicar(
        TOPIC_ESTADO,
        "RESET1"
    );


    ESP_LOGI(
        TAG,
        "RESET JUEGO 1"
    );
}


// ============================================================
// RESET JUEGO 2
// ============================================================

static void reset_juego2(void)
{
    /*
     * RESET2 solamente funciona
     * estando dentro de JUEGO2.
     */

    if (
        modo != MODO_JUEGO2
    )
    {
        return;
    }


    estado = ESPERANDO_INICIO;

    juego2_activo = false;

    pasos_juego2 = 0;

    ultimo_boton = 1;


    led_apagar();


    mqtt_publicar(
        TOPIC_J2_RESULTADO,
        "0"
    );


    mqtt_publicar(
        TOPIC_ESTADO,
        "RESET2"
    );


    ESP_LOGI(
        TAG,
        "RESET JUEGO 2"
    );
}


// ============================================================
// JUEGO 1 - INICIAR
// ============================================================

static void iniciar_juego1(void)
{
    if (
        modo != MODO_JUEGO1
    )
    {
        return;
    }


    if (
        estado != ESPERANDO_INICIO
    )
    {
        return;
    }


    estado = ESPERANDO_LED;


    tiempo_inicio_espera =
        esp_timer_get_time();


    /*
     * Tiempo aleatorio:
     *
     * 1 a 5 segundos
     */

    delay_random =
        1000000 +
        (
            esp_random() %
            4000000
        );


    boton1_suelto = false;


    led_apagar();


    mqtt_publicar(
        TOPIC_ESTADO,
        "ESPERANDO_LED"
    );


    ESP_LOGI(
        TAG,
        "JUEGO 1 INICIADO"
    );
}


// ============================================================
// JUEGO 1 - ENCENDER LED
// ============================================================

static void iniciar_reaccion(void)
{
    tiempo_led_encendido =
        esp_timer_get_time();


    led_encender();


    estado =
        ESPERANDO_ACCION;


    boton1_suelto = false;


    mqtt_publicar(
        TOPIC_ESTADO,
        "LISTO"
    );


    ESP_LOGI(
        TAG,
        "LED ENCENDIDO"
    );
}


// ============================================================
// JUEGO 1 - REACCION 1
// ============================================================
//
// LED -> SOLTAR B1
//
// ============================================================

static void reaccion_boton1_suelto(void)
{
    if (
        boton1_suelto
    )
    {
        return;
    }


    boton1_suelto = true;


    tiempo_suelta_boton1 =
        esp_timer_get_time();


    int64_t tiempo_us =
        tiempo_suelta_boton1 -
        tiempo_led_encendido;


    float tiempo_s =
        tiempo_us /
        1000000.0f;


    char resultado[20];


    snprintf(
        resultado,
        sizeof(resultado),
        "%.3f",
        tiempo_s
    );


    ESP_LOGI(
        TAG,
        "REACCION 1 = %s s",
        resultado
    );


    /*
     * Publicar REACCION 1.
     */

    mqtt_publicar(
        TOPIC_J1_REACCION1,
        resultado
    );


    /*
     * NO SE CALCULA PROMEDIO AQUI.
     *
     * El promedio es de REACCION 2.
     */
}


// ============================================================
// JUEGO 1 - REACCION 2
// ============================================================
//
// SOLTAR B1 -> PRESIONAR B2
//
// ============================================================

static void reaccion_boton2(void)
{
    if (
        !boton1_suelto
    )
    {
        return;
    }


    int64_t tiempo_actual =
        esp_timer_get_time();


    int64_t tiempo_us =
        tiempo_actual -
        tiempo_suelta_boton1;


    float tiempo_s =
        tiempo_us /
        1000000.0f;


    char resultado[20];


    snprintf(
        resultado,
        sizeof(resultado),
        "%.3f",
        tiempo_s
    );


    ESP_LOGI(
        TAG,
        "REACCION 2 = %s s",
        resultado
    );


    // ========================================================
    // PUBLICAR REACCION 2
    // ========================================================

    mqtt_publicar(
        TOPIC_J1_REACCION2,
        resultado
    );


    // ========================================================
    // GUARDAR REACCION 2
    // ========================================================

    tiempos_reaccion2[
        indice_reaccion2
    ] = tiempo_s;


    indice_reaccion2++;


    if (
        indice_reaccion2 >=
        NUM_JUGADAS_PROMEDIO
    )
    {
        indice_reaccion2 = 0;
    }


    if (
        cantidad_reacciones2 <
        NUM_JUGADAS_PROMEDIO
    )
    {
        cantidad_reacciones2++;
    }


    // ========================================================
    // PROMEDIO
    // ========================================================
    //
    // SOLO SE PUBLICA AL COMPLETAR 10 JUGADAS.
    //
    // ========================================================

    if (
        cantidad_reacciones2 ==
        NUM_JUGADAS_PROMEDIO
    )
    {
        float suma = 0.0f;


        for (
            int i = 0;
            i < NUM_JUGADAS_PROMEDIO;
            i++
        )
        {
            suma +=
                tiempos_reaccion2[i];
        }


        float promedio =
            suma /
            NUM_JUGADAS_PROMEDIO;


        char promedio_texto[20];


        snprintf(
            promedio_texto,
            sizeof(promedio_texto),
            "%.3f",
            promedio
        );


        mqtt_publicar(
            TOPIC_J1_PROMEDIO,
            promedio_texto
        );


        ESP_LOGI(
            TAG,
            "PROMEDIO DE 10 REACCIONES 2 = %s s",
            promedio_texto
        );
    }


    // ========================================================
    // TERMINAR JUGADA
    // ========================================================

    led_apagar();


    estado =
        JUEGO_TERMINADO;


    mqtt_publicar(
        TOPIC_ESTADO,
        "FINALIZADO"
    );
}


// ============================================================
// FALSA SALIDA
// ============================================================

static void falsa_salida(void)
{
    led_apagar();


    estado =
        JUEGO_TERMINADO;


    boton1_suelto = false;


    mqtt_publicar(
        TOPIC_J1_FALSA_SALIDA,
        "1"
    );


    mqtt_publicar(
        TOPIC_ESTADO,
        "FALSA_SALIDA"
    );


    ESP_LOGW(
        TAG,
        "FALSA SALIDA"
    );
}


// ============================================================
// JUEGO 2 - INICIAR
// ============================================================

static void iniciar_juego2(void)
{
    if (
        modo != MODO_JUEGO2
    )
    {
        return;
    }


    if (
        estado != ESPERANDO_INICIO
    )
    {
        return;
    }


    juego2_activo = true;


    pasos_juego2 = 0;


    /*
     * El primer boton esperado
     * es BOTON 1.
     */

    ultimo_boton = 1;


    tiempo_inicio_juego2 =
        esp_timer_get_time();


    led_encender();


    estado =
        ESPERANDO_ACCION;


    mqtt_publicar(
        TOPIC_ESTADO,
        "JUEGO2_INICIADO"
    );


    ESP_LOGI(
        TAG,
        "JUEGO 2 INICIADO - 1 SEGUNDO"
    );
}


// ============================================================
// JUEGO 2 - CAMBIO DE BOTON
// ============================================================

static void juego2_boton(int boton)
{
    if (
        !juego2_activo
    )
    {
        return;
    }


    /*
     * Si se presiona el mismo boton
     * nuevamente, no cuenta.
     */

    if (
        boton ==
        ultimo_boton
    )
    {
        return;
    }


    /*
     * Cambio valido:
     *
     * 1 -> 2
     * 2 -> 1
     */

    if (
        (
            ultimo_boton == 1 &&
            boton == 2
        )
        ||
        (
            ultimo_boton == 2 &&
            boton == 1
        )
    )
    {
        pasos_juego2++;


        ultimo_boton =
            boton;


        ESP_LOGI(
            TAG,
            "PASO JUEGO 2 = %d",
            pasos_juego2
        );
    }
}


// ============================================================
// JUEGO 2 - FINALIZAR
// ============================================================

static void finalizar_juego2(void)
{
    juego2_activo = false;


    led_apagar();


    char resultado[20];


    snprintf(
        resultado,
        sizeof(resultado),
        "%d",
        pasos_juego2
    );


    /*
     * PUBLICAR RESULTADO
     */

    mqtt_publicar(
        TOPIC_J2_RESULTADO,
        resultado
    );


    mqtt_publicar(
        TOPIC_ESTADO,
        "JUEGO2_FINALIZADO"
    );


    estado =
        JUEGO_TERMINADO;


    ESP_LOGI(
        TAG,
        "JUEGO 2 RESULTADO = %d",
        pasos_juego2
    );
}


// ============================================================
// MQTT EVENT HANDLER
// ============================================================

static void mqtt_event_handler(
    void *handler_args,
    esp_event_base_t base,
    int32_t event_id,
    void *event_data
)
{
    esp_mqtt_event_handle_t event =
        (esp_mqtt_event_handle_t)event_data;


    switch (
        event->event_id
    )
    {
        // ====================================================
        // CONECTADO
        // ====================================================

        case MQTT_EVENT_CONNECTED:

            ESP_LOGI(
                TAG,
                "MQTT CONECTADO"
            );


            mqtt_conectado = true;


            esp_mqtt_client_subscribe(
                mqtt_client,
                TOPIC_COMANDO,
                1
            );


            ESP_LOGI(
                TAG,
                "Suscrito a: %s",
                TOPIC_COMANDO
            );


            mqtt_publicar(
                TOPIC_ESTADO,
                "MENU"
            );


            break;


        // ====================================================
        // DESCONECTADO
        // ====================================================

        case MQTT_EVENT_DISCONNECTED:

            mqtt_conectado = false;


            ESP_LOGW(
                TAG,
                "MQTT DESCONECTADO"
            );


            break;


        // ====================================================
        // RECIBIR MQTT
        // ====================================================

        case MQTT_EVENT_DATA:
        {
            char topic[128];

            char mensaje[128];


            int topic_len =
                event->topic_len;


            int data_len =
                event->data_len;


            if (
                topic_len >=
                sizeof(topic)
            )
            {
                topic_len =
                    sizeof(topic) - 1;
            }


            if (
                data_len >=
                sizeof(mensaje)
            )
            {
                data_len =
                    sizeof(mensaje) - 1;
            }


            memcpy(
                topic,
                event->topic,
                topic_len
            );


            topic[topic_len] =
                '\0';


            memcpy(
                mensaje,
                event->data,
                data_len
            );


            mensaje[data_len] =
                '\0';


            ESP_LOGI(
                TAG,
                "MQTT RECIBIDO: %s",
                mensaje
            );


            /*
             * Solo aceptar mensajes
             * del topic de comando.
             */

            if (
                strcmp(
                    topic,
                    TOPIC_COMANDO
                ) != 0
            )
            {
                break;
            }


            // =================================================
            // MENU
            // =================================================

            if (
                strcmp(
                    mensaje,
                    "MENU"
                ) == 0
            )
            {
                volver_menu();

                break;
            }


            // =================================================
            // JUEGO 1
            // =================================================

            if (
                strcmp(
                    mensaje,
                    "JUEGO1"
                ) == 0
            )
            {
                entrar_juego1();

                break;
            }


            // =================================================
            // JUEGO 2
            // =================================================

            if (
                strcmp(
                    mensaje,
                    "JUEGO2"
                ) == 0
            )
            {
                entrar_juego2();

                break;
            }


            // =================================================
            // RESET 1
            // =================================================

            if (
                strcmp(
                    mensaje,
                    "RESET1"
                ) == 0
            )
            {
                reset_juego1();

                break;
            }


            // =================================================
            // RESET 2
            // =================================================

            if (
                strcmp(
                    mensaje,
                    "RESET2"
                ) == 0
            )
            {
                reset_juego2();

                break;
            }


            // =================================================
            // BOTON 1
            // =================================================

            if (
                strcmp(
                    mensaje,
                    "BOTON1"
                ) == 0
            )
            {
                if (
                    modo ==
                    MODO_JUEGO1
                )
                {
                    if (
                        estado ==
                        ESPERANDO_INICIO
                    )
                    {
                        iniciar_juego1();
                    }
                }

                else if (
                    modo ==
                    MODO_JUEGO2
                )
                {
                    if (
                        estado ==
                        ESPERANDO_INICIO
                    )
                    {
                        iniciar_juego2();
                    }
                    else
                    {
                        juego2_boton(1);
                    }
                }


                break;
            }


            // =================================================
            // SOLTAR BOTON 1
            // =================================================

            if (
                strcmp(
                    mensaje,
                    "SOLTAR1"
                ) == 0
            )
            {
                if (
                    modo ==
                    MODO_JUEGO1
                )
                {
                    if (
                        estado ==
                        ESPERANDO_LED
                    )
                    {
                        falsa_salida();
                    }

                    else if (
                        estado ==
                        ESPERANDO_ACCION
                    )
                    {
                        reaccion_boton1_suelto();
                    }
                }


                break;
            }


            // =================================================
            // BOTON 2
            // =================================================

            if (
                strcmp(
                    mensaje,
                    "BOTON2"
                ) == 0
            )
            {
                if (
                    modo ==
                    MODO_JUEGO1
                )
                {
                    if (
                        estado ==
                        ESPERANDO_ACCION
                    )
                    {
                        reaccion_boton2();
                    }
                }

                else if (
                    modo ==
                    MODO_JUEGO2
                )
                {
                    juego2_boton(2);
                }


                break;
            }


            break;
        }


        default:

            break;
    }
}


// ============================================================
// MQTT START
// ============================================================

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg =
    {
        .broker.address.uri =
            MQTT_BROKER_URI
    };


    mqtt_client =
        esp_mqtt_client_init(
            &mqtt_cfg
        );


    esp_mqtt_client_register_event(
        mqtt_client,
        ESP_EVENT_ANY_ID,
        mqtt_event_handler,
        NULL
    );


    esp_mqtt_client_start(
        mqtt_client
    );
}


// ============================================================
// WIFI EVENT
// ============================================================

static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
)
{
    if (
        event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_START
    )
    {
        esp_wifi_connect();
    }


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


    else if (
        event_base == IP_EVENT &&
        event_id == IP_EVENT_STA_GOT_IP
    )
    {
        ESP_LOGI(
            TAG,
            "WiFi conectado"
        );


        if (
            mqtt_client == NULL
        )
        {
            mqtt_app_start();
        }
    }
}


// ============================================================
// WIFI INIT
// ============================================================

static void wifi_init(void)
{
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
        esp_wifi_init(
            &cfg
        )
    );


    ESP_ERROR_CHECK(
        esp_event_handler_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &wifi_event_handler,
            NULL
        )
    );


    ESP_ERROR_CHECK(
        esp_event_handler_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &wifi_event_handler,
            NULL
        )
    );


    wifi_config_t wifi_config =
    {
        .sta =
        {
            .ssid =
                WIFI_SSID,

            .password =
                WIFI_PASSWORD
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


    ESP_LOGI(
        TAG,
        "Conectando a WiFi: %s",
        WIFI_SSID
    );
}


// ============================================================
// GPIO INIT
// ============================================================

static void gpio_init_juego(void)
{
    gpio_config_t botones =
    {
        .pin_bit_mask =
            (1ULL << PIN_BOTON1) |
            (1ULL << PIN_BOTON2),

        .mode =
            GPIO_MODE_INPUT,

        .pull_up_en =
            GPIO_PULLUP_ENABLE,

        .pull_down_en =
            GPIO_PULLDOWN_DISABLE,

        .intr_type =
            GPIO_INTR_DISABLE
    };


    ESP_ERROR_CHECK(
        gpio_config(
            &botones
        )
    );


    gpio_config_t led =
    {
        .pin_bit_mask =
            (1ULL << PIN_LED),

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
        gpio_config(
            &led
        )
    );


    gpio_set_level(
        PIN_LED,
        0
    );


    /*
     * Inicializar estados anteriores.
     */

    boton1_anterior =
        gpio_get_level(
            PIN_BOTON1
        );


    boton2_anterior =
        gpio_get_level(
            PIN_BOTON2
        );
}


// ============================================================
// TASK PRINCIPAL
// ============================================================

static void juego_task(void *pv)
{
    ESP_LOGI(
        TAG,
        "Sistema listo - MENU"
    );


    while (1)
    {
        int boton1 =
            gpio_get_level(
                PIN_BOTON1
            );


        int boton2 =
            gpio_get_level(
                PIN_BOTON2
            );


        /*
         * Detectar nueva pulsacion.
         *
         * Antes: SUELTO
         * Ahora: PRESIONADO
         *
         * Eso significa que el boton acaba
         * de ser presionado.
         */

        bool nueva_pulsacion_boton1 =
            (
                boton1_anterior ==
                SUELTO
            )
            &&
            (
                boton1 ==
                PRESIONADO
            );


        bool nueva_pulsacion_boton2 =
            (
                boton2_anterior ==
                SUELTO
            )
            &&
            (
                boton2 ==
                PRESIONADO
            );


        /*
         * Detectar que B1 acaba de soltarse.
         */

        bool nueva_suelta_boton1 =
            (
                boton1_anterior ==
                PRESIONADO
            )
            &&
            (
                boton1 ==
                SUELTO
            );


        // ====================================================
        // MENU
        // ====================================================

        if (
            modo ==
            MODO_MENU
        )
        {
            /*
             * Los botones físicos no hacen nada
             * en el menu.
             */
        }


        // ====================================================
        // JUEGO 1
        // ====================================================

        else if (
            modo ==
            MODO_JUEGO1
        )
        {
            switch (estado)
            {
                // ------------------------------------------------
                // ESPERANDO INICIO
                // ------------------------------------------------

                case ESPERANDO_INICIO:

                    if (
                        nueva_pulsacion_boton1
                    )
                    {
                        iniciar_juego1();
                    }

                    break;


                // ------------------------------------------------
                // ESPERANDO LED
                // ------------------------------------------------

                case ESPERANDO_LED:

                    /*
                     * Si B1 se suelta antes
                     * del LED = falsa salida.
                     */

                    if (
                        nueva_suelta_boton1
                    )
                    {
                        falsa_salida();

                        break;
                    }


                    if (
                        esp_timer_get_time() -
                        tiempo_inicio_espera >=
                        delay_random
                    )
                    {
                        iniciar_reaccion();
                    }

                    break;


                // ------------------------------------------------
                // LED ENCENDIDO
                // ------------------------------------------------

                case ESPERANDO_ACCION:

                    /*
                     * B1 se suelta.
                     */

                    if (
                        nueva_suelta_boton1
                    )
                    {
                        reaccion_boton1_suelto();
                    }


                    /*
                     * B2 se presiona.
                     */

                    if (
                        nueva_pulsacion_boton2
                    )
                    {
                        reaccion_boton2();
                    }

                    break;


                // ------------------------------------------------
                // TERMINADO
                // ------------------------------------------------

                case JUEGO_TERMINADO:

                    /*
                     * Esperar que ambos botones
                     * esten sueltos antes de
                     * permitir otra jugada.
                     */

                    if (
                        boton1 ==
                        SUELTO
                        &&
                        boton2 ==
                        SUELTO
                    )
                    {
                        estado =
                            ESPERANDO_INICIO;


                        mqtt_publicar(
                            TOPIC_ESTADO,
                            "ESPERANDO_INICIO"
                        );
                    }

                    break;
            }
        }


        // ====================================================
        // JUEGO 2
        // ====================================================

        else if (
            modo ==
            MODO_JUEGO2
        )
        {
            // ------------------------------------------------
            // ESPERANDO INICIO
            // ------------------------------------------------

            if (
                !juego2_activo
            )
            {
                if (
                    nueva_pulsacion_boton1
                )
                {
                    iniciar_juego2();
                }
            }


            // ------------------------------------------------
            // JUEGO ACTIVO
            // ------------------------------------------------

            else
            {
                /*
                 * BOTON 1 FISICO
                 */

                if (
                    nueva_pulsacion_boton1
                )
                {
                    juego2_boton(1);
                }


                /*
                 * BOTON 2 FISICO
                 */

                if (
                    nueva_pulsacion_boton2
                )
                {
                    juego2_boton(2);
                }


                /*
                 * Verificar 1 segundo.
                 */

                if (
                    esp_timer_get_time() -
                    tiempo_inicio_juego2 >=
                    5000000
                )
                {
                    finalizar_juego2();
                }
            }
        }


        /*
         * Guardar estados anteriores.
         */

        boton1_anterior =
            boton1;

        boton2_anterior =
            boton2;


        vTaskDelay(
            pdMS_TO_TICKS(5)
        );
    }
}


// ============================================================
// MAIN
// ============================================================

void app_main(void)
{
    esp_err_t ret =
        nvs_flash_init();


    if (
        ret ==
        ESP_ERR_NVS_NO_FREE_PAGES
        ||
        ret ==
        ESP_ERR_NVS_NEW_VERSION_FOUND
    )
    {
        ESP_ERROR_CHECK(
            nvs_flash_erase()
        );


        ret =
            nvs_flash_init();
    }


    ESP_ERROR_CHECK(
        ret
    );


    gpio_init_juego();


    wifi_init();


    xTaskCreate(
        juego_task,
        "juego_task",
        8192,
        NULL,
        5,
        NULL
    );
}