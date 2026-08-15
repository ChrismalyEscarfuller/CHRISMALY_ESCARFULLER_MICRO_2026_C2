#pragma once

#include <stdbool.h>
#include "mqtt_client.h"

typedef void (*mqtt_cmd_cb_t)(const char *data, int len);

typedef struct {
    char broker_uri[128];
    char client_id[64];
    char username[32];
    char password[32];
    bool connected;
} mqtt_handler_t;

void mqtt_handler_init(mqtt_handler_t *mqtt);
void mqtt_handler_start(mqtt_handler_t *mqtt);
void mqtt_handler_set_callback(mqtt_cmd_cb_t cb);
bool mqtt_handler_publish(const char *topic, const char *data);
bool mqtt_handler_is_connected(void);
