#include <stdbool.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board_pins.h"
#include "ui_buttons.h"

#define POLL_MS 10
#define DEBOUNCE_MS 30

typedef struct {
    gpio_num_t gpio;
    ui_button_t id;
    int candidate;
    int stable;
    TickType_t changed_at;
} button_state_t;

static button_state_t buttons[] = {
    {.gpio = BUTTON_UP_GPIO, .id = UI_BUTTON_UP},
    {.gpio = BUTTON_OK_GPIO, .id = UI_BUTTON_OK},
    {.gpio = BUTTON_DOWN_GPIO, .id = UI_BUTTON_DOWN},
};
static ui_button_press_cb_t press_callback;
static void *press_context;
static bool started;

static void button_task(void *arg)
{
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        TickType_t now = xTaskGetTickCount();
        for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
            button_state_t *b = &buttons[i];
            int level = gpio_get_level(b->gpio);
            if (level != b->candidate) {
                b->candidate = level;
                b->changed_at = now;
            }
            if (b->candidate != b->stable &&
                (TickType_t)(now - b->changed_at) >= pdMS_TO_TICKS(DEBOUNCE_MS)) {
                b->stable = b->candidate;
                if (b->stable == 0) {
                    press_callback(b->id, press_context);
                }
            }
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(POLL_MS));
    }
}

esp_err_t ui_buttons_init(ui_button_press_cb_t callback, void *context)
{
    if (!callback) return ESP_ERR_INVALID_ARG;
    if (started) return ESP_ERR_INVALID_STATE;
    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << BUTTON_UP_GPIO) | (1ULL << BUTTON_OK_GPIO) |
                        (1ULL << BUTTON_DOWN_GPIO),
        .mode = GPIO_MODE_INPUT,
        /* GPIO35/36/39 have no internal pulls. External 10k to 3.3V required. */
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) return err;
    press_callback = callback;
    press_context = context;
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
        /* A button held at boot must be released before its first event. */
        buttons[i].candidate = buttons[i].stable = gpio_get_level(buttons[i].gpio);
        buttons[i].changed_at = xTaskGetTickCount();
    }
    if (xTaskCreate(button_task, "ui_buttons", 2048, NULL, 1, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    started = true;
    return ESP_OK;
}
