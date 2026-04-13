#include "mqtt_pub.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "mqtt_client.h"

#define TAG "mqtt"
#define CONNECTED_BIT BIT0

static esp_mqtt_client_handle_t s_client;
static EventGroupHandle_t s_evt;

static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        xEventGroupSetBits(s_evt, CONNECTED_BIT);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED");
        xEventGroupClearBits(s_evt, CONNECTED_BIT);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
        break;
    default:
        break;
    }
}

esp_err_t mqtt_pub_start(const char *uri, const char *user, const char *pass)
{
    s_evt = xEventGroupCreate();

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = uri,
        .credentials.username = (user && user[0]) ? user : NULL,
        .credentials.authentication.password = (pass && pass[0]) ? pass : NULL,
    };

    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) return ESP_FAIL;

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(
        s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_client));

    ESP_LOGI(TAG, "connecting to %s ...", uri);
    EventBits_t bits = xEventGroupWaitBits(
        s_evt, CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(15000));

    return (bits & CONNECTED_BIT) ? ESP_OK : ESP_ERR_TIMEOUT;
}

int mqtt_pub_publish(const char *topic, const char *payload)
{
    int len = (int)strlen(payload);
    int msg_id = esp_mqtt_client_publish(s_client, topic, payload, len, 1, 0);
    ESP_LOGI(TAG, "published %d bytes to %s (msg_id=%d)", len, topic, msg_id);
    return msg_id;
}

int mqtt_pub_publish_retained(const char *topic, const char *payload)
{
    int len = (int)strlen(payload);
    int msg_id = esp_mqtt_client_publish(s_client, topic, payload, len, 1, 1);
    ESP_LOGI(TAG, "published RETAINED %d bytes to %s (msg_id=%d)", len, topic, msg_id);
    return msg_id;
}
