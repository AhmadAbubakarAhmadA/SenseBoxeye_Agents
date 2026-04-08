idf_component_register(
    SRCS "main.c" "wifi_sta.c" "mqtt_pub.c" "sen66.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_driver_i2c esp_wifi esp_event esp_netif nvs_flash
             esp_timer
)
