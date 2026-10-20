idf_component_register(SRCS "main.c" "display.c" "buttons.c" "ui.c"
                           "status.c" "ethernet.c" "services.c" "dashboard.c"
                           "neighbors.c" "discovery.c" "button_logic.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_i2c esp_driver_gpio esp_timer esp_hw_support
                  esp_system spi_flash esp_app_format esp_eth esp_event esp_netif
                  esp_wifi esp_http_server nvs_flash
    EMBED_TXTFILES "dashboard.html")
