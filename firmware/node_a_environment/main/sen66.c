// Native ESP-IDF SEN66 driver — no Arduino dependency.
// Protocol reference: Sensirion SEN66 datasheet, I2C interface section.
//
// Commands used:
//   0x0021  start_continuous_measurement       (no args, 50ms exec)
//   0x0104  stop_measurement                   (no args, 1000ms exec)
//   0x0202  get_data_ready                     (no args, 20ms exec, returns 1 word)
//   0x0300  read_measured_values_as_integers   (no args, 20ms exec, returns 9 words)
//
// Each word in a SEN66 response is 2 data bytes followed by 1 CRC byte (CRC-8/0x31, init 0xFF).

#include "sen66.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

#define TAG            "sen66"
#define I2C_PORT       I2C_NUM_0
#define I2C_SDA_GPIO   2
#define I2C_SCL_GPIO   1
#define I2C_FREQ_HZ    100000
#define SEN66_ADDR     0x6B

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;

static uint8_t crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static esp_err_t send_cmd(uint16_t cmd)
{
    uint8_t buf[2] = { (uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFF) };
    return i2c_master_transmit(s_dev, buf, 2, 100);
}

static esp_err_t read_words(uint16_t cmd, uint16_t *out, size_t n_words, uint32_t exec_ms)
{
    esp_err_t err = send_cmd(cmd);
    if (err != ESP_OK) return err;

    vTaskDelay(pdMS_TO_TICKS(exec_ms));

    uint8_t buf[27]; // up to 9 words * 3 bytes
    if (n_words * 3 > sizeof buf) return ESP_ERR_NO_MEM;

    err = i2c_master_receive(s_dev, buf, n_words * 3, 200);
    if (err != ESP_OK) return err;

    for (size_t i = 0; i < n_words; i++) {
        uint8_t *w = &buf[i * 3];
        if (crc8(w, 2) != w[2]) {
            ESP_LOGW(TAG, "CRC mismatch on word %u", (unsigned)i);
            return ESP_ERR_INVALID_CRC;
        }
        out[i] = (uint16_t)((w[0] << 8) | w[1]);
    }
    return ESP_OK;
}

esp_err_t sen66_init(void)
{
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
            .device_address = SEN66_ADDR,
            .scl_speed_hz = I2C_FREQ_HZ,
        };
        ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev));
    }

    // Stop any previous measurement (idempotent), wait, then start.
    send_cmd(0x0104);
    vTaskDelay(pdMS_TO_TICKS(1000));

    esp_err_t err = send_cmd(0x0021);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "start measurement failed: %s", esp_err_to_name(err));
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_LOGI(TAG, "SEN66 continuous measurement started");
    return ESP_OK;
}

esp_err_t sen66_read(sen66_reading_t *out)
{
    if (!out || !s_dev) return ESP_ERR_INVALID_STATE;

    // Optional: poll get_data_ready (0x0202). Skipped — we just call at 5s cadence
    // and the SEN66 produces one new sample per second, so data is always ready.

    uint16_t w[9];
    esp_err_t err = read_words(0x0300, w, 9, 20);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "read measured values failed: %s", esp_err_to_name(err));
        return err;
    }

    // Per SEN66 datasheet (read_measured_values_as_integers):
    //  w[0] PM1.0       uint16  ug/m3 / 10
    //  w[1] PM2.5       uint16  ug/m3 / 10
    //  w[2] PM4.0       uint16  ug/m3 / 10
    //  w[3] PM10        uint16  ug/m3 / 10
    //  w[4] humidity    int16   %RH    / 100
    //  w[5] temperature int16   degC   / 200
    //  w[6] VOC index   int16          / 10
    //  w[7] NOx index   int16          / 10
    //  w[8] CO2         uint16  ppm    (no scale)
    out->pm1           = w[0] / 10.0f;
    out->pm2_5         = w[1] / 10.0f;
    out->pm4           = w[2] / 10.0f;
    out->pm10          = w[3] / 10.0f;
    out->humidity_rh   = (int16_t)w[4] / 100.0f;
    out->temperature_c = (int16_t)w[5] / 200.0f;
    out->voc_index     = (int16_t)w[6] / 10.0f;
    out->nox_index     = (int16_t)w[7] / 10.0f;
    out->co2_ppm       = (float)w[8];

    ESP_LOGI(TAG, "co2=%.0f ppm  T=%.2fC  RH=%.1f%%  pm2.5=%.1f  voc=%.0f  nox=%.0f",
             out->co2_ppm, out->temperature_c, out->humidity_rh,
             out->pm2_5, out->voc_index, out->nox_index);
    return ESP_OK;
}
