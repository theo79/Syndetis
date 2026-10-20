#ifndef TEST_SYSTEM_H
#define TEST_SYSTEM_H
#include <stdint.h>
static inline uint32_t esp_get_free_heap_size(void) { return 100000; }
static inline uint32_t esp_get_minimum_free_heap_size(void) { return 90000; }
static inline int esp_reset_reason(void) { return 1; }
static inline const char *esp_get_idf_version(void) { return "v6.1"; }
#endif
