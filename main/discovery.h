#pragma once
#include "esp_err.h"
#include "discovery_logic.h"
#include "discovery_names.h"
typedef enum { DISC_IDLE, DISC_RUNNING, DISC_COMPLETE, DISC_ERROR } discovery_state_t;
typedef struct {
    discovery_state_t state;
    discovery_subnet_t subnet;
    uint32_t processed, found, start_uptime_ms, completion_uptime_ms, duration_ms;
    bool enriching;
    uint32_t checked;
    uint32_t icmp_setup_failures, icmp_measured, icmp_unmeasured;
    char error[80];
    discovery_hostname_status_t hostname;
} discovery_status_t;
esp_err_t discovery_init(void);
esp_err_t discovery_start(void);
void discovery_get(discovery_status_t *status);
/* Short protected copy only: no task-owned mutex or network-dependent wait. */
void discovery_snapshot(discovery_status_t *status, discovery_device_t *devices);
bool discovery_device(unsigned index, discovery_device_t *device);
const char *discovery_state_name(discovery_state_t state);
