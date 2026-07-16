#pragma once

#define I2C_PORT        I2C_NUM_0
#define I2C_SDA_GPIO    6
#define I2C_SCL_GPIO    7
#define I2C_FREQ_HZ     400000

// Instala el driver I2C una sola vez (lo usan lcd_i2c.c y mpu6050.c)
void i2c_bus_init(void);
