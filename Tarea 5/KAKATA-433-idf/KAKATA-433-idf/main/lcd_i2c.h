#pragma once
#include <stdint.h>

// Driver minimo para un LCD 16x2 HD44780 con backpack I2C PCF8574
// (reemplazo casero de la libreria LiquidCrystal_I2C de Arduino).
// Requiere que i2c_bus_init() ya se haya llamado antes.

#define LCD_I2C_ADDR   0x27
#define LCD_COLS       16
#define LCD_ROWS       2

void lcd_init(void);
void lcd_clear(void);
void lcd_set_cursor(uint8_t col, uint8_t row);
void lcd_print(const char *str);
void lcd_print_char(char c);
