#include "button_logic.h"
#include "board.h"
#include <string.h>

void button_logic_reset(button_logic_t *s, uint8_t pressed)
{
    memset(s, 0, sizeof(*s));
    s->released_since = -1;
    for (int i = 0; i < 3; ++i) {
        s->keys[i].raw = s->keys[i].stable = (pressed & (1U << i)) != 0;
        s->keys[i].changed = -1;
    }
}

uint8_t button_logic_update(button_logic_t *s, uint8_t pressed, int64_t now)
{
    uint8_t events = 0;
    for (int i = 0; i < 3; ++i) {
        bool down = (pressed & (1U << i)) != 0;
        if (down != s->keys[i].raw || s->keys[i].changed < 0) {
            s->keys[i].raw = down; s->keys[i].changed = now;
        }
        if (now - s->keys[i].changed >= BUTTON_DEBOUNCE_MS && s->keys[i].stable != down) {
            s->keys[i].stable = down;
            if (s->armed) {
                if (i < 2 && down) events |= 1U << i;
                else if (i == 2 && down) {
                    s->ok_since = now; s->ok_active = true; s->long_sent = false;
                } else if (i == 2 && s->ok_active) {
                    if (!s->long_sent) events |= now - s->ok_since >= BUTTON_HOLD_MS ? BUTTON_BACK : BUTTON_OK;
                    s->ok_active = false;
                }
            }
        }
    }
    if (!s->armed) {
        if (pressed) s->released_since = -1;
        else if (s->released_since < 0) s->released_since = now;
        else if (now - s->released_since >= BUTTON_DEBOUNCE_MS) s->armed = true;
        return 0;
    }
    if (s->ok_active && s->keys[2].stable && !s->long_sent && now - s->ok_since >= BUTTON_HOLD_MS) {
        s->long_sent = true;
        events |= BUTTON_BACK;
    }
    return events;
}
