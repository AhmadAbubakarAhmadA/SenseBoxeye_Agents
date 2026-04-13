// ICM-20948 IMU driver — standalone (owns I2C bus).
// senseBox Eye QWIIC: SDA=GPIO2, SCL=GPIO1.
// WHO_AM_I at 0x00 → 0xEA, address 0x68.

#include "icm20948.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

#define TAG          "icm20948"
#define ICM_ADDR     0x68
#define I2C_PORT     I2C_NUM_0
#define I2C_SDA_GPIO 2
#define I2C_SCL_GPIO 1
#define I2C_FREQ_HZ  100000

#define REG_WHO_AM_I     0x00
#define REG_PWR_MGMT_1   0x06
#define REG_ACCEL_XOUT_H 0x2D
#define REG_GYRO_XOUT_H  0x33
#define REG_BANK_SEL     0x7F

#define LSB_TO_G   (1.0f / 16384.0f)  // ±2 g default
#define LSB_TO_DPS (1.0f / 131.0f)    // ±250 dps default

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;

static esp_err_t reg_read(uint8_t reg, uint8_t *out, size_t n)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, out, n, 100);
}

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_dev, buf, 2, 100);
}

esp_err_t icm20948_init(void)
{
    // Create I2C bus (Node C has no SEN66, so we own the bus)
    if (!s_bus) {
        i2c_master_bus_config_t bus_cfg = {
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .i2c_port = I2C_PORT,
            .scl_io_num = I2C_SCL_GPIO,
            .sda_io_num = I2C_SDA_GPIO,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_bus));
    }

    if (!s_dev) {
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address  = ICM_ADDR,
            .scl_speed_hz    = I2C_FREQ_HZ,
        };
        ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev));
    }

    // User bank 0
    reg_write(REG_BANK_SEL, 0x00);

    uint8_t id = 0;
    esp_err_t err = reg_read(REG_WHO_AM_I, &id, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WHO_AM_I read failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "ICM-20948 WHO_AM_I = 0x%02X (expect 0xEA)", id);
    if (id != 0xEA) {
        ESP_LOGW(TAG, "unexpected WHO_AM_I — continuing anyway");
    }

    // Wake from sleep, auto-select best clock
    err = reg_write(REG_PWR_MGMT_1, 0x01);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PWR_MGMT_1 write failed: %s", esp_err_to_name(err));
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_LOGI(TAG, "ICM-20948 initialized");
    return ESP_OK;
}

esp_err_t icm20948_read(imu_reading_t *out)
{
    if (!out || !s_dev) return ESP_ERR_INVALID_STATE;

    uint8_t a[6], g[6];
    esp_err_t err = reg_read(REG_ACCEL_XOUT_H, a, 6);
    if (err != ESP_OK) return err;
    err = reg_read(REG_GYRO_XOUT_H, g, 6);
    if (err != ESP_OK) return err;

    // Big-endian: H then L
    int16_t ax = (int16_t)((a[0] << 8) | a[1]);
    int16_t ay = (int16_t)((a[2] << 8) | a[3]);
    int16_t az = (int16_t)((a[4] << 8) | a[5]);
    int16_t rx = (int16_t)((g[0] << 8) | g[1]);
    int16_t ry = (int16_t)((g[2] << 8) | g[3]);
    int16_t rz = (int16_t)((g[4] << 8) | g[5]);

    out->ax_g   = ax * LSB_TO_G;
    out->ay_g   = ay * LSB_TO_G;
    out->az_g   = az * LSB_TO_G;
    out->gx_dps = rx * LSB_TO_DPS;
    out->gy_dps = ry * LSB_TO_DPS;
    out->gz_dps = rz * LSB_TO_DPS;
    return ESP_OK;
}
