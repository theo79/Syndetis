#pragma once
#include "esp_err.h"

// Runs once on the service worker. No Ethernet esp_netif or IP stack attachment.
esp_err_t ethernet_start(void);
