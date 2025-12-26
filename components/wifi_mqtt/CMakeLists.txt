idf_component_register(
    SRCS "src/wifi_mqtt.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi mqtt nvs_flash esp_netif esp_event
)
