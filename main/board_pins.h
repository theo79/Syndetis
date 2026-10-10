#pragma once

#include "driver/gpio.h"

#define OLED_SDA_GPIO GPIO_NUM_32
#define OLED_SCL_GPIO GPIO_NUM_33
#define BUTTON_UP_GPIO GPIO_NUM_39
#define BUTTON_OK_GPIO GPIO_NUM_36
#define BUTTON_DOWN_GPIO GPIO_NUM_35

/* Reserved only: no SPI bus or SD device is initialized. */
#define RESERVED_SD_CLK_GPIO GPIO_NUM_17
#define RESERVED_SD_MOSI_GPIO GPIO_NUM_14
#define RESERVED_SD_MISO_GPIO GPIO_NUM_4
#define RESERVED_SD_CS_GPIO GPIO_NUM_15
