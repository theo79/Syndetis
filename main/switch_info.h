#pragma once
#include "switch_info_parse.h"
#include "esp_eth.h"
#include "esp_netif.h"

#define SWITCH_INFO_CAPTURE_SLOTS 4
#define SWITCH_INFO_QUEUE_DEPTH 4
#define SWITCH_INFO_WORKER_STACK 4096
#define SWITCH_INFO_WORKER_PRIORITY 1
typedef struct {
    uint32_t candidates, queued, parsed, malformed, oversize, pool_full, queue_full;
    uint32_t stale_generation, expired, filter_failures;
    uint32_t cdp_candidates, cdp_parsed, cdp_malformed;
} switch_info_counters_t;
typedef struct {
    bool available;
    int setup_error;
    bool cdp_available;
    int cdp_setup_error;
    switch_info_counters_t counters;
    switch_neighbor_t neighbor;
    uint32_t free_heap, minimum_free_heap, worker_stack_free_bytes;
} switch_info_status_t;
/* Call once after netif attachment, before Ethernet start. Optional errors leave
 * normal shared RX routing intact. No live callback swapping supported. */
esp_err_t switch_info_init(esp_eth_handle_t driver, esp_netif_t *netif);
/* Copies a current snapshot; expired/session-stale neighbor is zeroed. */
void switch_info_get(switch_info_status_t *out);

/* Shared RX router owns dispatch; true means consumed/freed once. */
bool switch_info_capture(uint8_t *buffer, uint32_t length);
