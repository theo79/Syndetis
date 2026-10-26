#pragma once
#include <stdint.h>
#include <stdbool.h>

enum { BUTTON_UP = 1, BUTTON_DOWN = 2, BUTTON_OK = 4, BUTTON_BACK = 8 };
enum { BUTTON_UP_REPEAT = 16, BUTTON_DOWN_REPEAT = 32 };
void buttons_init(void);
// Discard prior input and require ALL buttons released for the debounce time.
void buttons_require_release(void);
// UP/DOWN on stable press; short OK on release, BACK once at a 500 ms OK hold.
uint8_t buttons_poll(int64_t now_ms);
void buttons_set_repeat(bool enabled);
