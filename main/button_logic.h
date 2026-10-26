#pragma once
#include <stdbool.h>
#include "buttons.h"

typedef struct {
    struct { bool raw, stable; int64_t changed, pressed_since, repeated; } keys[3];
    bool armed, ok_active, long_sent;
    int64_t released_since, ok_since;
    bool repeat_enabled;
} button_logic_t;

void button_logic_reset(button_logic_t *state, uint8_t pressed);
uint8_t button_logic_update(button_logic_t *state, uint8_t pressed, int64_t now_ms);
