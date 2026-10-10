#pragma once

#include "esp_err.h"

typedef enum {
    UI_BUTTON_UP,   /* previous/up */
    UI_BUTTON_OK,   /* select/execute */
    UI_BUTTON_DOWN, /* next/down */
} ui_button_t;

/* Called once per debounced press from the polling task. Keep callbacks short.
 * A future menu can enqueue these events without changing the GPIO handling.
 * Initialize once at startup; callback and context must outlive the task.
 */
typedef void (*ui_button_press_cb_t)(ui_button_t button, void *context);
esp_err_t ui_buttons_init(ui_button_press_cb_t callback, void *context);
