#pragma once
#include "esp_err.h"

/* SH1106 128x64, address 0x3C, GPIO32/33, visible RAM columns 2..129.
 * Init and drawing belong to a single UI task/startup owner; not thread safe.
 * Eight rows of up to 21 characters each, rendered with a 5x7 uppercase font.
 * No networking, menu state or event handlers live in this driver.
 */
esp_err_t ui_oled_init(void);
esp_err_t ui_oled_show_lines(const char *lines[8]);
