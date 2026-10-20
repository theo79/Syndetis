#include "board.h"
#include "buttons.h"
#include "display.h"
#include "ui.h"
#include "services.h"
#include <inttypes.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

void app_main(void)
{
    buttons_init();
    esp_err_t err = display_init();
    if (err != ESP_OK) {
        ESP_LOGE("Syndetis", "OLED initialization failed: %s. Check SDA32/SCL33, power and board.h.",
                 esp_err_to_name(err));
        return; // Do not reboot endlessly when the display is missing.
    }
    ui_draw_splash(0);
    ESP_ERROR_CHECK(display_present());
    // Start at completion of the first visible frame, excluding I2C setup.
    int64_t start = now_ms();
    int last_stage = 0;
    for (;;) {
        int64_t elapsed = now_ms() - start;
        if (elapsed >= SPLASH_DURATION_MS) break;
        int stage = elapsed < 900 ? elapsed / 180 : elapsed < 1050 ? 5 : elapsed < 1350 ? 6 : 7;
        if (stage != last_stage) {
            ui_draw_splash(elapsed);
            ESP_ERROR_CHECK(display_present());
            last_stage = stage;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGI("Syndetis", "Splash elapsed: %" PRId64 " ms", now_ms() - start);
    buttons_require_release();
    ui_init();
    ui_draw();
    ESP_ERROR_CHECK(display_present());
    services_start();
    int64_t last_refresh = now_ms();
    for (;;) {
        int64_t now = now_ms();
        uint8_t pressed = buttons_poll(now);
        if (pressed) ui_input(pressed);
        if (pressed || now - last_refresh >= 250) {
            ui_draw();
            ESP_ERROR_CHECK(display_present());
            last_refresh = now_ms();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
