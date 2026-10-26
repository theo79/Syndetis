#ifndef TEST_FLASH_H
#define TEST_FLASH_H
#include <stdint.h>
#include "esp_err.h"
static inline esp_err_t esp_flash_get_size(void *chip,uint32_t *n) {(void)chip;*n=4194304;return 0;}
#endif
