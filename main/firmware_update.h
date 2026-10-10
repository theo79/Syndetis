#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_http_server.h"

typedef enum { OTA_IDLE, OTA_RECEIVING, OTA_FAILED, OTA_SUCCESS } ota_phase_t;
typedef struct {
    bool unlocked;
    unsigned seconds_left;
    ota_phase_t phase;
    unsigned percent;
    unsigned revision;
    char message[96];
} ota_status_t;

/* Only the physical menu calls this; there is no HTTP unlock endpoint. */
bool firmware_update_authorize(void);
void firmware_update_status(ota_status_t *status);
esp_err_t firmware_update_upload(httpd_req_t *request);
/* Confirm only after core startup succeeds, without needing cable or OLED. */
esp_err_t firmware_update_confirm_boot(void);
