idf_component_register(SRCS "main.c" "display.c" "buttons.c" "ui.c"
                           "status.c" "ethernet.c" "services.c" "dashboard.c"
                           "neighbors.c" "discovery.c" "button_logic.c"
                           "profinet.c" "profinet_dcp.c"
                           "dhcp_test.c" "network_policy.c" "web_guard.c" "manual_ipv4.c"
                           "network_ping.c" "ping_packet.c" "ota_engine.c" "ota_update.c" "ota_http_policy.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_i2c esp_driver_gpio esp_timer esp_hw_support
                  esp_system spi_flash esp_app_format esp_eth esp_event esp_netif
                  esp_wifi esp_http_server esp_https_server app_update mbedtls nvs_flash lwip
    EMBED_TXTFILES "dashboard.html" "update.html")
