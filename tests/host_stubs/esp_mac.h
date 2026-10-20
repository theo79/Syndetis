#ifndef TEST_MAC_H
#define TEST_MAC_H
#include <stdint.h>
#include <string.h>
#include "esp_err.h"
static inline esp_err_t esp_efuse_mac_get_default(uint8_t *mac) { memset(mac,0,6);return 0; }
#endif
