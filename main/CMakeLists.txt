idf_component_register(
    SRCS "app_main.c" "ui_buttons.c" "ui_oled.c" "ui_menu.c"
         "network_status.c" "web_server.c" "firmware_update.c" "autotest.c"
         "discovery.c" "oui_vendor.c" "discovery_names.c" "ptr_dns.c" "diagnostic.c" "switch_info.c" "switch_info_parse.c" "ui_switch_info.c"
         "switch_info_cdp.c" "switch_info_select.c"
         "ethernet_rx.c" "profinet.c" "ui_profinet.c"
    PRIV_REQUIRES esp_eth esp_netif esp_event nvs_flash esp_driver_gpio esp_driver_i2c
                  esp_http_server app_update esp_app_format esp_timer bootloader_support lwip
    EMBED_TXTFILES "web_index.html"
)
