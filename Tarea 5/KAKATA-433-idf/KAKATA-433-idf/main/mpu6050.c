#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "i2c_bus.h"
#include "mpu6050.h"

#define MPU_ADDR 0x68

static esp_err_t mpu_write_reg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg, val };
    return i2c_master_write_to_device(I2C_PORT, MPU_ADDR, buf, 2, pdMS_TO_TICKS(50));
}

void mpu6050_init(void) {
    mpu_write_reg(0x6B, 0x00); // PWR_MGMT_1: salir de sleep, clock interno
    mpu_write_reg(0x1C, 0x00); // ACCEL_CONFIG: +-2g
    mpu_write_reg(0x1B, 0x00); // GYRO_CONFIG: +-250 dps
}

bool mpu6050_read(int16_t *ax, int16_t *ay, int16_t *az,
                   int16_t *gx, int16_t *gy, int16_t *gz) {
    uint8_t reg = 0x3B; // ACCEL_XOUT_H
    uint8_t buf[14];

    esp_err_t err = i2c_master_write_read_device(I2C_PORT, MPU_ADDR, &reg, 1,
                                                   buf, sizeof(buf), pdMS_TO_TICKS(50));
    if (err != ESP_OK) return false;

    *ax = (int16_t)((buf[0]  << 8) | buf[1]);
    *ay = (int16_t)((buf[2]  << 8) | buf[3]);
    *az = (int16_t)((buf[4]  << 8) | buf[5]);
    // buf[6],buf[7] = temperatura, no se usa
    *gx = (int16_t)((buf[8]  << 8) | buf[9]);
    *gy = (int16_t)((buf[10] << 8) | buf[11]);
    *gz = (int16_t)((buf[12] << 8) | buf[13]);
    return true;
}
