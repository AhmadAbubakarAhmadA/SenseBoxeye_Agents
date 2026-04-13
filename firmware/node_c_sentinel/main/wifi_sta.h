#pragma once
#include "esp_err.h"

// Bring up Wi-Fi STA, connect to CONFIG_NODE_C_WIFI_SSID, and block
// until an IP is acquired (or fail).
esp_err_t wifi_sta_connect_blocking(void);
