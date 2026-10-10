#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "ui_buttons.h"
esp_err_t ui_menu_init(bool display_ready);
/* Nonblocking button callback; navigation/drawing runs in the menu task. */
void ui_menu_button(ui_button_t button, void *context);
