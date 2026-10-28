// Exercise the production splash and polling loop with transient display faults.
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#define ESP_LOGI(...) ((void)0)
#include "../main/main.c"
static jmp_buf done;
static int64_t time_us;
static int splash_attempts, runtime_attempts, polls, health, gate_depth;
static bool started;
void ota_update_start(void) {}
void buttons_init(void) {}
void buttons_require_release(void) {}
void buttons_set_repeat(bool repeat) {(void)repeat;}
uint8_t buttons_poll(int64_t now) {(void)now;return ++polls==1?BUTTON_OK:0;}
esp_err_t display_init(void) {return ESP_OK;}
esp_err_t display_present(void) {
    if (!started) return ++splash_attempts<=2?ESP_FAIL:ESP_OK;
    assert(gate_depth==1);
    return ++runtime_attempts==1?ESP_FAIL:ESP_OK;
}
void ui_draw_splash(int64_t elapsed) {(void)elapsed;}
void ui_init(void) {}
void ui_draw(void) {}
void ui_input(uint8_t pressed) {assert(pressed==BUTTON_OK);}
bool ui_editing(void) {return false;}
bool ui_refresh_needed(void) {return false;} // unchanged OTA progress screen
void services_start(void) {started=true;}
bool services_boot_healthy(void) {return true;}
bool ota_update_ui_begin(void) {assert(!gate_depth);++gate_depth;return true;}
void ota_update_ui_end(void) {assert(gate_depth==1);--gate_depth;}
void ota_update_health_frame(bool healthy) {
    assert(healthy && runtime_attempts==2 && !display_retry);++health;
}
int64_t esp_timer_get_time(void) {return time_us;}
void vTaskDelay(unsigned ticks) {
    time_us+=(int64_t)ticks*1000;
    if(time_us>=4500000)longjmp(done,1);
}
int main(void) {
    if(!setjmp(done))app_main();
    assert(started && splash_attempts>2 && polls>100);
    assert(runtime_attempts==2 && health==1 && !display_retry && !gate_depth);
    puts("PASS: production main loop survives splash/runtime I2C faults, polls buttons, retries unchanged frames, acknowledges only successful frames and releases UI gate");
}
