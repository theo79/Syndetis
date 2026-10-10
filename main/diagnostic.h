#pragma once
#include <stdbool.h>
#include <stdint.h>
uint32_t diagnostic_acquire(void);
void diagnostic_release(uint32_t token);
bool diagnostic_current(uint32_t token);
