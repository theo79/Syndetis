#ifndef TEST_CHIP_INFO_H
#define TEST_CHIP_INFO_H
typedef struct { int model; unsigned cores, revision; } esp_chip_info_t;
static inline void esp_chip_info(esp_chip_info_t *c) { c->model=1;c->cores=2;c->revision=300; }
#endif
