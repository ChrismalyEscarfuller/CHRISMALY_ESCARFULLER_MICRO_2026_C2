# KAKATA-433 — proyecto ESP-IDF puro (sin Arduino)

## Requisitos
- Extensión **"Espressif IDF"** en VS Code (no la de PlatformIO).
- Al instalarla, ella misma te ofrece descargar el toolchain de ESP-IDF
  (elige la versión **v5.x**, ideal v5.1 o más nueva).

## Antes de compilar
Edita en `main/main.c` estas tres líneas con tus datos reales:
```c
#define WIFI_SSID          "TU_RED_WIFI"
#define WIFI_PASS          "TU_PASSWORD"
#define MQTT_BROKER_URI    "mqtt://192.168.1.100:1883"
```

## Pasos en VS Code
1. Abre esta carpeta completa (`KAKATA-433-idf`) con `File > Open Folder`.
2. En la barra de estado de abajo, en el icono de "Espressif IDF: Set
   Target", elige **esp32s3**.
3. La primera vez que compiles, IDF va a descargar solo el componente
   `espressif/esp_tinyusb` (declarado en `main/idf_component.yml`), no
   hace falta instalar nada a mano.
4. Compila con el icono de "Build" (o `idf.py build` en la terminal de
   IDF).
5. Conecta el ESP32-S3 por USB-C y usa "Flash" y luego "Monitor".

## Qué reemplaza a qué (Arduino -> ESP-IDF)
| Arduino                     | ESP-IDF                                  |
|------------------------------|-------------------------------------------|
| `Wire.h`                     | `driver/i2c.h` (ver `i2c_bus.c`)          |
| `LiquidCrystal_I2C`          | `lcd_i2c.c` (driver casero HD44780/PCF8574)|
| `analogRead()`                | `driver/adc.h` (`adc1_get_raw`)           |
| `digitalRead()` / `pinMode()`| `driver/gpio.h`                           |
| `WiFi.h`                     | `esp_wifi.h` + `esp_netif.h`              |
| `PubSubClient`               | `mqtt_client.h` (componente `mqtt` de IDF)|
| `Preferences`                | `nvs_flash.h` / `nvs.h`                   |
| `USBHIDGamepad`              | TinyUSB nativo: `usb_descriptors.c` + `usb_hid.c` |
| `millis()`                    | `esp_timer_get_time() / 1000`             |

## Nota importante sobre el USB HID
La parte de `usb_descriptors.c` / `tusb_config.h` / `usb_hid.c` es la
más delicada: ahí se define "a mano" cómo la PC identifica al ESP32-S3
como un gamepad. Está escrita siguiendo el patrón oficial de los
ejemplos de TinyUSB/Espressif, pero si al conectar el control por
USB-C la PC no lo reconoce como gamepad, es el primer lugar donde hay
que revisar (sobre todo `sdkconfig` — puede que tu versión de IDF use
otros nombres de opción para habilitar TinyUSB, revísalo con
`idf.py menuconfig` bajo "Component config -> TinyUSB").
