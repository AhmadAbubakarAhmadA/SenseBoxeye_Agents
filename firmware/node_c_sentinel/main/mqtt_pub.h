#pragma once
#include "esp_err.h"

esp_err_t mqtt_pub_start(const char *uri, const char *user, const char *pass);
int mqtt_pub_publish(const char *topic, const char *payload);
int mqtt_pub_publish_retained(const char *topic, const char *payload);
