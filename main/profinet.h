#pragma once
#include "esp_eth.h"
#include "profinet_dcp.h"
typedef enum { PN_IDLE, PN_RUNNING, PN_COMPLETE, PN_CANCELLED, PN_NETWORK_CHANGED, PN_TX_ERROR } pn_state_t;
typedef enum { PN_START_OK, PN_START_UNAVAILABLE, PN_START_LINK_DOWN, PN_START_BUSY, PN_START_COOLDOWN } pn_start_t;
typedef struct {
    uint32_t scan_attempts,tx_posted,tx_accepted,tx_failed,tx_callback_timeout;
    uint32_t candidates,accepted,parsed,malformed,wrong_xid,stale,oversize,pool_full,queue_full;
    uint32_t candidate_limit,device_limit,duplicate_blocks;
} pn_counters_t;
typedef struct {
    pn_state_t state;
    bool available,partial;
    unsigned count;
    uint32_t xid,token,physical_epoch,revision,duration_ms,packets_sent;
    uint32_t free_heap,minimum_free_heap,worker_stack_free_bytes;
    int tx_error;
    pn_counters_t counters;
} profinet_status_t;
esp_err_t profinet_init(esp_eth_handle_t driver);
/* Only the local physical menu calls start. HTTP is read-only. */
pn_start_t profinet_start(void);
void profinet_cancel(void);
void profinet_get(profinet_status_t *out);
void profinet_snapshot(profinet_status_t *out,profinet_device_t *devices);
bool profinet_device(unsigned index,profinet_device_t *out);
/* Shared router: true means original buffer was consumed/freed exactly once. */
bool profinet_capture(uint8_t *buffer,uint32_t length);
const char *profinet_state_name(pn_state_t state);
