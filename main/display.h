#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64

esp_err_t display_init(void);
uint8_t display_address(void);
void display_clear(void);
void display_rect(int x, int y, int width, int height, bool filled, bool on);
void display_text(int x, int y, const char *text, int scale, bool on);
esp_err_t display_present(void);
