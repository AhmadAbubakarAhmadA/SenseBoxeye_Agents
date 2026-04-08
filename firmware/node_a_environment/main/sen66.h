#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float pm1, pm2_5, pm4, pm10;
    float temperature_c, humidity_rh;
    float voc_index, nox_index;
    float co2_ppm;
} sen66_reading_t;

esp_err_t sen66_init(void);
esp_err_t sen66_read(sen66_reading_t *out);

#ifdef __cplusplus
}
#endif
