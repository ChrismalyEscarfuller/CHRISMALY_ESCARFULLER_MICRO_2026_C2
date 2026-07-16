#pragma once
#include <stdint.h>

// Inicializa la pila USB nativa en modo HID gamepad
void usb_hid_init(void);

// Envia un reporte de gamepad. x,y,z,rz van de -100 a 100 (mismo rango
// que se usaba en la version Arduino); buttons es una mascara de 32 bits.
void usb_hid_send(int8_t x, int8_t y, int8_t z, int8_t rz, uint32_t buttons);
