#include "esp_err.h"
#include "tinyusb.h"
#include "class/hid/hid_device.h"
#include "usb_hid.h"

extern uint8_t const desc_configuration[];

void usb_hid_init(void) {
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,        // usa tud_descriptor_device_cb()
        .string_descriptor = NULL,        // usa tud_descriptor_string_cb()
        .external_phy = false,
        .configuration_descriptor = desc_configuration,
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
}

void usb_hid_send(int8_t x, int8_t y, int8_t z, int8_t rz, uint32_t buttons) {
    if (!tud_mounted() || !tud_hid_ready()) return;

    hid_gamepad_report_t report = {
        .x = x, .y = y, .z = z, .rz = rz,
        .rx = 0, .ry = 0,
        .hat = 0,
        .buttons = buttons,
    };
    tud_hid_report(0, &report, sizeof(report));
}
