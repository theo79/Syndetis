#pragma once
#include "ota_engine.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    ota_state_t state;
    char message[96];
    size_t received, total;
    uint32_t minimum_heap, worker_stack_free;
} ota_update_status_t;
void ota_update_start(void);
// Only physical OLED navigation calls request_session. Nonblocking.
bool ota_update_request_session(void);
void ota_update_cancel(void);
ota_update_status_t ota_update_status(void);
// Main task calls after a successful OLED frame and completed UI initialization.
void ota_update_health_frame(bool services_healthy);

void ota_update_register_events(void);

// The UI task owns drawing. This gate serializes its full I2C frame with OTA flash.
bool ota_update_ui_begin(void);
void ota_update_ui_end(void);
static inline unsigned ota_update_percent(const ota_update_status_t *s)
{
    if(!s->total)return 0;
    return s->received>=s->total?100:(unsigned)((uint64_t)s->received*100/s->total);
}
