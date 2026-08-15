// =====================================================================
//  Descriptores USB para el gamepad KAKATA-433
//  Esto es lo que en Arduino hacia "por dentro" la libreria
//  USBHIDGamepad; aqui hay que declararlo a mano con TinyUSB.
// =====================================================================
#include <string.h>
#include "tusb.h"
#include "class/hid/hid_device.h"

// VID/PID de desarrollo de Espressif (validos para proyectos propios /
// educativos; si algun dia lo vas a vender, hay que comprar un VID/PID
// propio en usb.org)
#define USB_VID   0x303A
#define USB_PID   0x8001

// ---------- Device descriptor ----------
static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *) &desc_device;
}

// ---------- HID report descriptor: gamepad estandar de TinyUSB ----------
// X, Y, Z, Rz, Rx, Ry (int8 cada uno), hat (4 bits) y 32 botones.
static uint8_t const desc_hid_report[] = {
    TUD_HID_REPORT_DESC_GAMEPAD()
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void) instance;
    return desc_hid_report;
}

// ---------- Configuration descriptor ----------
enum { ITF_NUM_HID, ITF_NUM_TOTAL };
#define EPNUM_HID           0x81
#define CONFIG_TOTAL_LEN    (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_NONE,
                        sizeof(desc_hid_report), EPNUM_HID,
                        CFG_TUD_HID_EP_BUFSIZE, 5),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void) index;
    return desc_configuration;
}

// ---------- String descriptors ----------
static char const *string_desc_arr[] = {
    (const char[]) { 0x09, 0x04 },  // 0: idioma (English - US)
    "KAKATA",                        // 1: fabricante
    "KAKATA-433 Gamepad",            // 2: producto
    "433001",                        // 3: numero de serie
};

static uint16_t _desc_str[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void) langid;
    uint8_t chr_count;

    if (index == 0) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) {
            return NULL;
        }
        const char *str = string_desc_arr[index];
        chr_count = (uint8_t) strlen(str);
        if (chr_count > 31) chr_count = 31;
        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = str[i];
        }
    }

    _desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}

// ---------- Callbacks obligatorios de la clase HID ----------
// No usamos "get report" real (el host no nos pide leer nada de vuelta).
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                hid_report_type_t report_type,
                                uint8_t *buffer, uint16_t reqlen) {
    (void) instance; (void) report_id; (void) report_type;
    (void) buffer; (void) reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                            hid_report_type_t report_type,
                            uint8_t const *buffer, uint16_t bufsize) {
    (void) instance; (void) report_id; (void) report_type;
    (void) buffer; (void) bufsize;
}
