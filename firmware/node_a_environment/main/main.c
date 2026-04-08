// Node A — SEN66 readings → MQTT publisher

#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "wifi_sta.h"
#include "mqtt_pub.h"
#include "sen66.h"
#include "spatial_meta.h"

#define TAG "node_a"

void app_main(void)
{
    ESP_LOGI(TAG, "Node A starting (sensor_id=%s)", CONFIG_NODE_A_SENSOR_ID);

    ESP_ERROR_CHECK(wifi_sta_connect_blocking());
    ESP_ERROR_CHECK(mqtt_pub_start(CONFIG_NODE_A_MQTT_URI,
                                   CONFIG_NODE_A_MQTT_USER,
                                   CONFIG_NODE_A_MQTT_PASS));
    if (sen66_init() != ESP_OK) {
        ESP_LOGE(TAG, "SEN66 init failed — will keep retrying every 5s");
    }

    char topic[64];
    snprintf(topic, sizeof topic, "sensors/%s/data", CONFIG_NODE_A_SENSOR_ID);

    while (1) {
        sen66_reading_t r;
        if (sen66_read(&r) == ESP_OK) {
            char buf[512];
            int64_t uptime_ms = esp_timer_get_time() / 1000;
            int n = snprintf(buf, sizeof buf,
                "{\"sensor_id\":\"%s\",\"timestamp_ms\":%" PRId64 ","
                "\"location\":{\"x\":%.2f,\"y\":%.2f,\"z\":%.2f,\"floor\":%d,\"room\":\"%s\"},"
                "\"data\":{\"co2_ppm\":%.0f,\"temperature_c\":%.2f,\"humidity_rh\":%.2f,"
                "\"voc_index\":%.0f,\"nox_index\":%.0f,"
                "\"pm1\":%.2f,\"pm2_5\":%.2f,\"pm4\":%.2f,\"pm10\":%.2f}}",
                CONFIG_NODE_A_SENSOR_ID, uptime_ms,
                NODE_A_X, NODE_A_Y, NODE_A_Z, NODE_A_FLOOR, NODE_A_ROOM,
                r.co2_ppm, r.temperature_c, r.humidity_rh,
                r.voc_index, r.nox_index,
                r.pm1, r.pm2_5, r.pm4, r.pm10);
            if (n > 0 && n < (int)sizeof buf) {
                mqtt_pub_publish(topic, buf);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
