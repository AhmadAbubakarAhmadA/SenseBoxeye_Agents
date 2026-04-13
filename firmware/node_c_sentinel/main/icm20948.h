#pragma once
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float ax_g;    // accel X (g)
    float ay_g;    // accel Y (g)
    float az_g;    // accel Z (g)
    float gx_dps;  // gyro X (deg/s)
    float gy_dps;  // gyro Y (deg/s)
    float gz_dps;  // gyro Z (deg/s)
} imu_reading_t;

// Initialize I2C bus (QWIIC SDA=2, SCL=1) and ICM-20948 on it.
esp_err_t icm20948_init(void);

// Read accelerometer + gyroscope.
esp_err_t icm20948_read(imu_reading_t *out);

#ifdef __cplusplus
}
#endif
