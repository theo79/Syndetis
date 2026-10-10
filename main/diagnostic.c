#include "freertos/FreeRTOS.h"
#include "diagnostic.h"
#include "diagnostic_rules.h"
#include "esp_log.h"
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static diagnostic_reservation_t state;
uint32_t diagnostic_acquire(void)
{
    portENTER_CRITICAL(&lock);
    uint32_t acquired=diagnostic_reserve(&state);
    portEXIT_CRITICAL(&lock);
    return acquired;
}
void diagnostic_release(uint32_t token)
{
    portENTER_CRITICAL(&lock);
    bool released=diagnostic_unreserve(&state, token);
    portEXIT_CRITICAL(&lock);
    if (released) ESP_LOGI("DIAGNOSTIC", "coordinator released (operation %u)", (unsigned)token);
}
bool diagnostic_current(uint32_t token)
{
    portENTER_CRITICAL(&lock);
    bool current=diagnostic_accept(token,state.active,0,0,true);
    portEXIT_CRITICAL(&lock);
    return current;
}
