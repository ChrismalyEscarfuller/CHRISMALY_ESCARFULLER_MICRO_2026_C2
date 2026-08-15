#include <string.h>
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"   // esp_rom_delay_us
#include "i2c_bus.h"
#include "lcd_i2c.h"

// Bits del backpack PCF8574 (mapa estandar usado por LiquidCrystal_I2C)
#define LCD_BACKLIGHT  0x08
#define LCD_ENABLE     0x04
#define LCD_RW         0x02
#define LCD_RS         0x01

static esp_err_t i2c_write_byte(uint8_t data) {
    return i2c_master_write_to_device(I2C_PORT, LCD_I2C_ADDR, &data, 1,
                                       pdMS_TO_TICKS(50));
}

static void lcd_pulse_enable(uint8_t data) {
    i2c_write_byte(data | LCD_ENABLE);
    esp_rom_delay_us(1);
    i2c_write_byte(data & ~LCD_ENABLE);
    esp_rom_delay_us(50);
}

static void lcd_write4bits(uint8_t nibble_with_flags) {
    uint8_t data = (nibble_with_flags & 0xF0) | LCD_BACKLIGHT
                   | (nibble_with_flags & (LCD_RS | LCD_RW));
    i2c_write_byte(data);
    lcd_pulse_enable(data);
}

static void lcd_send(uint8_t value, uint8_t mode) {
    lcd_write4bits((value & 0xF0) | mode);
    lcd_write4bits(((value << 4) & 0xF0) | mode);
}

static void lcd_command(uint8_t cmd) { lcd_send(cmd, 0); }
static void lcd_data(uint8_t data)   { lcd_send(data, LCD_RS); }

void lcd_init(void) {
    vTaskDelay(pdMS_TO_TICKS(50));

    // Secuencia de arranque en modo 4 bits (rutina estandar HD44780)
    lcd_write4bits(0x30); vTaskDelay(pdMS_TO_TICKS(5));
    lcd_write4bits(0x30); vTaskDelay(pdMS_TO_TICKS(1));
    lcd_write4bits(0x30); vTaskDelay(pdMS_TO_TICKS(1));
    lcd_write4bits(0x20); // pasa a modo 4 bits

    lcd_command(0x28); // 4 bits, 2 lineas, fuente 5x8
    lcd_command(0x0C); // display on, cursor off, blink off
    lcd_command(0x06); // entry mode: incrementa, sin shift
    lcd_clear();
}

void lcd_clear(void) {
    lcd_command(0x01);
    vTaskDelay(pdMS_TO_TICKS(2));
}

void lcd_set_cursor(uint8_t col, uint8_t row) {
    static const uint8_t row_offsets[LCD_ROWS] = {0x00, 0x40};
    if (row >= LCD_ROWS) row = LCD_ROWS - 1;
    lcd_command(0x80 | ((col + row_offsets[row]) & 0x3F));
}

void lcd_print(const char *str) {
    while (*str) lcd_data((uint8_t)(*str++));
}

void lcd_print_char(char c) {
    lcd_data((uint8_t)c);
}
