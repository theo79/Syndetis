#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    bool enriching, cancelled;
    uint32_t attempted, resolved, timeout, failed, duration_ms, packets_sent;
} discovery_hostname_status_t;
/* Existing discovery worker only. Core results are already published; retains
 * its diagnostic reservation for at most the small global enrichment budget. */
discovery_hostname_status_t discovery_names_run(uint32_t token, uint32_t revision, unsigned count);
bool discovery_hostname_result(uint32_t ip, const char *name, uint32_t token, uint32_t revision);
