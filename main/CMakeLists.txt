idf_component_register(SRCS "main.c" "blinker_cloud.c" "cloud_command.c" INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_timer
             esp_driver_gpio esp_hw_support mqtt esp_http_client mbedtls json lwip)
