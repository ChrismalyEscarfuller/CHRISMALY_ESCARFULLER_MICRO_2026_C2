#include "oled_display.h"
#include "config.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "OLED";
static oled_t *g_oled = NULL;

#define SSD1306_SETCONTRAST       0x81
#define SSD1306_DISPLAYALLON_RESUME 0xA4
#define SSD1306_NORMALDISPLAY     0xA6
#define SSD1306_DISPLAYOFF        0xAE
#define SSD1306_DISPLAYON         0xAF
#define SSD1306_SETDISPLAYOFFSET  0xD3
#define SSD1306_SETCOMPINS        0xDA
#define SSD1306_SETVCOMDETECT     0xDB
#define SSD1306_SETDISPLAYCLOCKDIV 0xD5
#define SSD1306_SETPRECHARGE      0xD9
#define SSD1306_SETMULTIPLEX      0xA8
#define SSD1306_SETLOWCOLUMN      0x00
#define SSDa1306_SETHIGHCOLUMN    0x10
#define SSD1306_SETSTARTLINE      0x40
#define SSD1306_MEMORYMODE        0x20
#define SSD1306_COLUMNADDR        0x21
#define SSD1306_PAGEADDR          0x22
#define SSD1306_COMSCANINC        0xC0
#define SSD1306_COMSCANDEC        0xC8
#define SSD1306_SEGREMAP          0xA0
#define SSD1306_CHARGEPUMP        0x8D

#define SSD1306_PAGES             8

static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x5F,0x00,0x00},
    {0x00,0x07,0x00,0x07,0x00},
    {0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12},
    {0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50},
    {0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00},
    {0x00,0x41,0x22,0x1C,0x00},
    {0x08,0x2A,0x1C,0x2A,0x08},
    {0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},
    {0x08,0x08,0x08,0x08,0x08},
    {0x00,0x60,0x60,0x00,0x00},
    {0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E},
    {0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},
    {0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},
    {0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30},
    {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},
    {0x06,0x49,0x49,0x29,0x1E},
    {0x00,0x36,0x36,0x00,0x00},
    {0x00,0x56,0x36,0x00,0x00},
    {0x00,0x08,0x14,0x22,0x41},
    {0x14,0x14,0x14,0x14,0x14},
    {0x41,0x22,0x14,0x08,0x00},
    {0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3E},
    {0x7E,0x11,0x11,0x11,0x7E},
    {0x7F,0x49,0x49,0x49,0x36},
    {0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},
    {0x7F,0x49,0x49,0x49,0x41},
    {0x7F,0x09,0x09,0x01,0x01},
    {0x3E,0x41,0x41,0x51,0x32},
    {0x7F,0x08,0x08,0x08,0x7F},
    {0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01},
    {0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x04,0x02,0x7F},
    {0x7F,0x04,0x08,0x10,0x7F},
    {0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},
    {0x3E,0x41,0x51,0x21,0x5E},
    {0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01},
    {0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F},
    {0x7F,0x20,0x18,0x20,0x7F},
    {0x63,0x14,0x08,0x14,0x63},
    {0x03,0x04,0x78,0x04,0x03},
    {0x61,0x51,0x49,0x45,0x43},
    {0x00,0x00,0x7F,0x41,0x41},
    {0x02,0x04,0x08,0x10,0x20},
    {0x41,0x41,0x7F,0x00,0x00},
    {0x04,0x02,0x01,0x02,0x04},
    {0x80,0x80,0x80,0x80,0x80},
};

static uint8_t framebuffer[OLED_WIDTH * OLED_HEIGHT / 8];

static void i2c_write_cmd(uint8_t cmd)
{
    i2c_cmd_handle_t handle = i2c_cmd_link_create();
    i2c_master_start(handle);
    i2c_master_write_byte(handle, (OLED_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(handle, 0x00, true);
    i2c_master_write_byte(handle, cmd, true);
    i2c_master_stop(handle);
    i2c_master_cmd_begin(I2C_MASTER_NUM, handle, pdMS_TO_TICKS(10));
    i2c_cmd_link_delete(handle);
}

static void i2c_write_data(uint8_t *data, size_t len)
{
    i2c_cmd_handle_t handle = i2c_cmd_link_create();
    i2c_master_start(handle);
    i2c_master_write_byte(handle, (OLED_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(handle, 0x40, true);
    i2c_master_write(handle, data, len, true);
    i2c_master_stop(handle);
    i2c_master_cmd_begin(I2C_MASTER_NUM, handle, pdMS_TO_TICKS(10));
    i2c_cmd_link_delete(handle);
}

static void ssd1306_init_seq(void)
{
    i2c_write_cmd(SSD1306_DISPLAYOFF);
    i2c_write_cmd(SSD1306_SETDISPLAYCLOCKDIV);
    i2c_write_cmd(0x80);
    i2c_write_cmd(SSD1306_SETMULTIPLEX);
    i2c_write_cmd(OLED_HEIGHT - 1);
    i2c_write_cmd(SSD1306_SETDISPLAYOFFSET);
    i2c_write_cmd(0x00);
    i2c_write_cmd(SSD1306_SETSTARTLINE | 0x00);
    i2c_write_cmd(SSD1306_CHARGEPUMP);
    i2c_write_cmd(0x14);
    i2c_write_cmd(SSD1306_MEMORYMODE);
    i2c_write_cmd(0x00);
    i2c_write_cmd(SSD1306_SEGREMAP | 0x01);
    i2c_write_cmd(SSD1306_COMSCANDEC);
    i2c_write_cmd(SSD1306_SETCOMPINS);
    i2c_write_cmd(0x12);
    i2c_write_cmd(SSD1306_SETCONTRAST);
    i2c_write_cmd(0xCF);
    i2c_write_cmd(SSD1306_SETPRECHARGE);
    i2c_write_cmd(0xF1);
    i2c_write_cmd(SSD1306_SETVCOMDETECT);
    i2c_write_cmd(0x40);
    i2c_write_cmd(SSD1306_DISPLAYALLON_RESUME);
    i2c_write_cmd(SSD1306_NORMALDISPLAY);
    i2c_write_cmd(SSD1306_DISPLAYON);
}

void oled_init(oled_t *oled)
{
    g_oled = oled;
    oled->width = OLED_WIDTH;
    oled->height = OLED_HEIGHT;
    oled->initialized = false;

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0));

    ssd1306_init_seq();
    memset(framebuffer, 0, sizeof(framebuffer));
    oled->initialized = true;
    ESP_LOGI(TAG, "OLED SSD1306 initialized (%dx%d)", OLED_WIDTH, OLED_HEIGHT);
}

void oled_clear(void)
{
    memset(framebuffer, 0, sizeof(framebuffer));
}

static void draw_pixel(int x, int y, bool white)
{
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) return;
    int page = y / 8;
    int bit = y % 8;
    if (white)
        framebuffer[x + page * OLED_WIDTH] |= (1 << bit);
    else
        framebuffer[x + page * OLED_WIDTH] &= ~(1 << bit);
}

void oled_draw_string(int x, int y, const char *str)
{
    while (*str) {
        uint8_t ch = *str - 32;
        if (ch >= sizeof(font5x7) / sizeof(font5x7[0])) ch = 0;
        if (ch == 0) ch = 0;
        for (int col = 0; col < 5; col++) {
            uint8_t line = font5x7[ch][col];
            for (int row = 0; row < 7; row++) {
                if (line & (1 << row))
                    draw_pixel(x + col, y + row, true);
                else
                    draw_pixel(x + col, y + row, false);
            }
        }
        draw_pixel(x + 5, y + 0, false);
        draw_pixel(x + 5, y + 1, false);
        draw_pixel(x + 5, y + 2, false);
        draw_pixel(x + 5, y + 3, false);
        draw_pixel(x + 5, y + 4, false);
        draw_pixel(x + 5, y + 5, false);
        draw_pixel(x + 5, y + 6, false);
        x += 6;
        str++;
    }
}

void oled_draw_line(int x0, int y0, int x1, int y1)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    while (1) {
        draw_pixel(x0, y0, true);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void oled_update(void)
{
    for (int page = 0; page < SSD1306_PAGES; page++) {
        i2c_write_cmd(0xB0 + page);
        i2c_write_cmd(0x00);
        i2c_write_cmd(0x10);
        i2c_write_data(&framebuffer[page * OLED_WIDTH], OLED_WIDTH);
    }
}

void oled_show_status(const char *state_str, bool wifi_ok, bool mqtt_ok, const char *extra)
{
    oled_clear();
    char line[22];
    snprintf(line, sizeof(line), "PORTON %s", state_str);
    oled_draw_string(0, 0, line);
    oled_draw_line(0, 10, OLED_WIDTH - 1, 10);
    oled_draw_string(0, 14, wifi_ok ? "WiFi: OK" : "WiFi: NO");
    oled_draw_string(0, 24, mqtt_ok ? "MQTT: OK" : "MQTT: NO");
    if (extra && strlen(extra) > 0) {
        oled_draw_string(0, 40, extra);
    }
    oled_update();
}

void oled_show_error(const char *msg)
{
    oled_clear();
    oled_draw_string(10, 10, "ERROR");
    oled_draw_line(0, 22, OLED_WIDTH - 1, 22);
    oled_draw_string(0, 28, msg);
    oled_update();
}
