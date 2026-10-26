#pragma once
#include <stdbool.h>
#include <stdint.h>

void ui_draw_splash(int64_t elapsed_ms);
void ui_init(void);
void ui_input(uint8_t buttons);
void ui_draw(void);
bool ui_editing(void);
