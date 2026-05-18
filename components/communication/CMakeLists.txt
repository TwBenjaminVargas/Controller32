idf_component_register(
    SRCS "EspNowCommunication.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi nvs_flash esp_netif freertos log
)