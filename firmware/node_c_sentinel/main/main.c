// Node C — Door sentinel: ICM-20948 vibration events → MQTT

#include <stdio.h>
#include <math.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "wifi_sta.h"
#include "mqtt_pub.h"
#include "icm20948.h"
#include "event_buffer.h"
#include "spatial_meta.h"

#define TAG "node_c"

// Threshold in g (converted from Kconfig milli-g)
#define VIBRATION_THRESH (CONFIG_NODE_C_VIBRATION_THRESHOLD / 1000.0f)
#define DEBOUNCE_MS      CONFIG_NODE_C_DEBOUNCE_MS

// Door state: inferred from vibration events.
// "open" if an event happened in the last 30s.
// "closed" if no event for >30s.
#define DOOR_OPEN_WINDOW_MS 30000

static const char *TOOL_MANIFEST_TEMPLATE =
    "{"
      "\"sensor_id\":\"%s\","
      "\"location\":{\"x\":%.2f,\"y\":%.2f,\"z\":%.2f,\"floor\":%d,\"room\":\"%s\"},"
      "\"tools\":["
        "{\"name\":\"detect_vibration_event\","
         "\"description\":\"Get recent vibration events detected by the IMU on the door frame between Lab B and the hallway, at position (9.5, 4.0). Each event represents a door opening or closing.\","
         "\"input_schema\":{\"type\":\"object\",\"properties\":{"
           "\"last_n\":{\"type\":\"integer\",\"description\":\"Number of recent events to return (default 20)\"},"
           "\"since_minutes\":{\"type\":\"integer\",\"description\":\"Only return events from the last N minutes\"}"
         "},\"required\":[]}},"
        "{\"name\":\"get_door_state\","
         "\"description\":\"Get inferred door state (open/closed) for the door between Lab B and hallway at position (9.5, 4.0), based on vibration pattern analysis.\","
         "\"input_schema\":{\"type\":\"object\",\"properties\":{},\"required\":[]}}"
      "]"
    "}";

static void publish_tool_manifest(void)
{
    char topic[64];
    snprintf(topic, sizeof topic, "sensors/%s/tools", CONFIG_NODE_C_SENSOR_ID);

    char buf[1024];
    int n = snprintf(buf, sizeof buf, TOOL_MANIFEST_TEMPLATE,
                     CONFIG_NODE_C_SENSOR_ID,
                     NODE_C_X, NODE_C_Y, NODE_C_Z, NODE_C_FLOOR, NODE_C_ROOM);
    if (n > 0 && n < (int)sizeof buf) {
        mqtt_pub_publish_retained(topic, buf);
    } else {
        ESP_LOGE(TAG, "tool manifest buffer too small (%d bytes)", n);
    }
}

static const char *infer_door_state(void)
{
    int64_t last_ts = event_buffer_last_ts();
    if (last_ts == 0) return "unknown";
    int64_t now_ms = esp_timer_get_time() / 1000;
    return (now_ms - last_ts < DOOR_OPEN_WINDOW_MS) ? "open" : "closed";
}

// Publish heartbeat every 5s with current state + recent event count
static void publish_heartbeat(const char *topic)
{
    const char *state = infer_door_state();
    int count = event_buffer_count();
    int64_t last_ts = event_buffer_last_ts();
    int64_t uptime_ms = esp_timer_get_time() / 1000;

    // Include last N events in heartbeat so agent can access via cached data
    vibration_event_t events[10];
    int n_events = event_buffer_get_since(events, 10, 300000);  // last 5 min

    char buf[1024];
    int off = snprintf(buf, sizeof buf,
        "{\"sensor_id\":\"%s\",\"timestamp_ms\":%" PRId64 ","
        "\"location\":{\"x\":%.2f,\"y\":%.2f,\"z\":%.2f,\"floor\":%d,\"room\":\"%s\"},"
        "\"data\":{\"door_state\":\"%s\",\"event_count\":%d,"
        "\"last_event_ms\":%" PRId64 ",\"events\":[",
        CONFIG_NODE_C_SENSOR_ID, uptime_ms,
        NODE_C_X, NODE_C_Y, NODE_C_Z, NODE_C_FLOOR, NODE_C_ROOM,
        state, count, last_ts);

    for (int i = 0; i < n_events && off < (int)sizeof(buf) - 80; i++) {
        if (i > 0) off += snprintf(buf + off, sizeof(buf) - off, ",");
        off += snprintf(buf + off, sizeof(buf) - off,
            "{\"ts\":%" PRId64 ",\"mag\":%.3f,\"type\":\"%s\"}",
            events[i].timestamp_ms, events[i].magnitude, events[i].type);
    }
    off += snprintf(buf + off, sizeof(buf) - off, "]}}");

    if (off > 0 && off < (int)sizeof buf) {
        mqtt_pub_publish(topic, buf);
    }
}

// IMU sampling task: runs at ~50 Hz, detects vibration spikes
static void imu_sample_task(void *arg)
{
    int64_t last_event_ms = 0;

    while (1) {
        imu_reading_t r;
        if (icm20948_read(&r) == ESP_OK) {
            float mag = sqrtf(r.ax_g * r.ax_g + r.ay_g * r.ay_g + r.az_g * r.az_g);
            float dev = fabsf(mag - 1.0f);

            if (dev > VIBRATION_THRESH) {
                int64_t now_ms = esp_timer_get_time() / 1000;
                if (now_ms - last_event_ms >= DEBOUNCE_MS) {
                    vibration_event_t evt = {
                        .timestamp_ms = now_ms,
                        .magnitude = dev,
                    };
                    snprintf(evt.type, sizeof evt.type, "door_event");
                    event_buffer_push(&evt);
                    last_event_ms = now_ms;
                    ESP_LOGI(TAG, "vibration event: dev=%.3fg (total=%d)",
                             dev, event_buffer_count());
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));  // ~50 Hz
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Node C starting (sensor_id=%s)", CONFIG_NODE_C_SENSOR_ID);

    ESP_ERROR_CHECK(wifi_sta_connect_blocking());
    ESP_ERROR_CHECK(mqtt_pub_start(CONFIG_NODE_C_MQTT_URI,
                                   CONFIG_NODE_C_MQTT_USER,
                                   CONFIG_NODE_C_MQTT_PASS));
    if (icm20948_init() != ESP_OK) {
        ESP_LOGE(TAG, "ICM-20948 init failed — will keep retrying");
    }

    event_buffer_init();

    // Advertise tools (retained)
    publish_tool_manifest();

    // Start IMU sampling on a dedicated high-priority task
    xTaskCreatePinnedToCore(imu_sample_task, "imu_sample", 4096, NULL, 5, NULL, 1);

    char topic[64];
    snprintf(topic, sizeof topic, "sensors/%s/data", CONFIG_NODE_C_SENSOR_ID);

    // Heartbeat loop — every 5s
    while (1) {
        publish_heartbeat(topic);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
