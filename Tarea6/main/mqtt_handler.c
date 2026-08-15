#include "mqtt_handler.h"
#include "config.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "MQTT";
static esp_mqtt_client_handle_t client = NULL;
static mqtt_cmd_cb_t command_callback = NULL;
static mqtt_handler_t *g_mqtt = NULL;

static void wifi_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi connecting to SSID: %s", WIFI_SSID);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected to %s", g_mqtt ? g_mqtt->broker_uri : "?");
        if (g_mqtt) g_mqtt->connected = true;
        esp_mqtt_client_subscribe(client, MQTT_TOPIC_COMMAND, 0);
        esp_mqtt_client_subscribe(client, MQTT_TOPIC_CONFIG, 0);
        esp_mqtt_client_publish(client, MQTT_TOPIC_STATUS, "online", 0, 1, 0);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected");
        if (g_mqtt) g_mqtt->connected = false;
        break;

    case MQTT_EVENT_DATA:
        if (command_callback && event->topic) {
            if (strncmp(event->topic, MQTT_TOPIC_COMMAND, event->topic_len) == 0 ||
                strncmp(event->topic, MQTT_TOPIC_CONFIG, event->topic_len) == 0) {
                command_callback(event->data, event->data_len);
            }
        }
        break;

    default:
        break;
    }
}

void mqtt_handler_init(mqtt_handler_t *mqtt)
{
    g_mqtt = mqtt;
    mqtt->connected = false;
    strncpy(mqtt->broker_uri, MQTT_BROKER_URI, sizeof(mqtt->broker_uri) - 1);
    strncpy(mqtt->client_id, MQTT_CLIENT_ID, sizeof(mqtt->client_id) - 1);
    mqtt->username[0] = '\0';
    mqtt->password[0] = '\0';

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init();
}

void mqtt_handler_start(mqtt_handler_t *mqtt)
{
    g_mqtt = mqtt;

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = mqtt->broker_uri,
        .credentials.client_id = mqtt->client_id,
        .session.protocol_ver = MQTT_PROTOCOL_V_5,
        .network.timeout_ms = 10000,
    };

    if (mqtt->username[0] && mqtt->password[0]) {
        mqtt_cfg.credentials.username = mqtt->username;
        mqtt_cfg.credentials.authentication.password = mqtt->password;
    }

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);

    ESP_LOGI(TAG, "MQTT 5.0 client starting -> %s", mqtt->broker_uri);
}

void mqtt_handler_set_callback(mqtt_cmd_cb_t cb)
{
    command_callback = cb;
}

bool mqtt_handler_publish(const char *topic, const char *data)
{
    if (!client || !g_mqtt || !g_mqtt->connected) return false;
    int msg_id = esp_mqtt_client_publish(client, topic, data, 0, 1, 0);
    return msg_id >= 0;
}

bool mqtt_handler_is_connected(void)
{
    return g_mqtt && g_mqtt->connected;
}
