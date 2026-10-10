#pragma once
#include "esp_eth.h"
#include "esp_netif.h"
esp_err_t ethernet_rx_init(esp_eth_handle_t driver,esp_netif_t *netif);
