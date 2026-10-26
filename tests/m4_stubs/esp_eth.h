#pragma once
#include <stddef.h>
#include "esp_err.h"
typedef void *esp_eth_handle_t;
esp_err_t esp_eth_transmit(void*,void*,size_t);
