#include "buttons.h"
#include "board.h"
#include "button_logic.h"
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_err.h"

static const gpio_num_t pins[] = {BOARD_UP, BOARD_DOWN, BOARD_OK};
static button_logic_t state;

static uint8_t read_pressed(void)
{
    uint8_t pressed = 0;
    for (int i = 0; i < 3; ++i)
        if (gpio_get_level(pins[i]) == 0) pressed |= 1U << i;
    return pressed;
}

void buttons_require_release(void)
{
    button_logic_reset(&state, read_pressed());
}

void buttons_init(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << BOARD_UP) | (1ULL << BOARD_DOWN) | (1ULL << BOARD_OK),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));
    buttons_require_release();
}

uint8_t buttons_poll(int64_t now_ms)
{
    return button_logic_update(&state, read_pressed(), now_ms);
}

void buttons_set_repeat(bool enabled) { state.repeat_enabled = enabled; }
