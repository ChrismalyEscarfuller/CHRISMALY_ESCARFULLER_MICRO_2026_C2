#pragma once
#include <stdint.h>
#include <stdbool.h>

void mpu6050_init(void);
bool mpu6050_read(int16_t *ax, int16_t *ay, int16_t *az,
                   int16_t *gx, int16_t *gy, int16_t *gz);
