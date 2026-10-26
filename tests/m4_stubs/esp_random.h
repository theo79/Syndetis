#pragma once
#include <stdint.h>
static uint32_t test_random_value = 0x80000000u;
static inline uint32_t esp_random(void) { return test_random_value; }
