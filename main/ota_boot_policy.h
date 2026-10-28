#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum { OTA_BOOT_WAIT, OTA_BOOT_CONFIRM, OTA_BOOT_ROLLBACK } ota_boot_action_t;
// Bounded startup checks: external links/clients/servers are deliberately absent.
static inline ota_boot_action_t ota_boot_action(uint64_t elapsed_ms, unsigned good_frames,
                                              bool nvs_ok, bool tasks_ok)
{
    if(elapsed_ms>=30000) return OTA_BOOT_ROLLBACK;
    if(elapsed_ms>=3000 && good_frames>=8 && nvs_ok && tasks_ok) return OTA_BOOT_CONFIRM;
    return OTA_BOOT_WAIT;
}
