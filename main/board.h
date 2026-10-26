#pragma once

// The only location for board wiring and OLED module adjustments.
#define BOARD_SDA 32
#define BOARD_SCL 33
#define BOARD_UP 36
#define BOARD_DOWN 39
#define BOARD_OK 5

// 0 = probe both SH1106 addresses and require exactly one ACK.
// Set to 0x3C or 0x3D only to resolve ambiguity; it is still probed.
#define OLED_ADDRESS 0
#define OLED_ROTATE_180 0
#define OLED_COLUMN_OFFSET 2
#define OLED_I2C_HZ 400000
#define BUTTON_DEBOUNCE_MS 30
#define BUTTON_HOLD_MS 500
#define SPLASH_DURATION_MS 3000

// WT32-ETH01 onboard LAN8720A and external oscillator (see README sources).
#define BOARD_ETH_PHY_ADDRESS 1
#define BOARD_ETH_MDC 23
#define BOARD_ETH_MDIO 18
#define BOARD_ETH_CLOCK_ENABLE 16
#define BOARD_ETH_CLOCK_INPUT 0
