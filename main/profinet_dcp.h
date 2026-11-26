#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    DCP_REQUEST_SIZE = 60, DCP_FRAME_LIMIT = 1522, DCP_CAPACITY = 32,
    DCP_WINDOW_MS = 5000, DCP_COOLDOWN_MS = 3000,
    DCP_NAME = 1, DCP_VENDOR = 2, DCP_IP = 4, DCP_ID = 8, DCP_ROLE = 16
};

typedef struct {
    uint8_t mac[6];
    char name[241], vendor[65];
    uint8_t ip[4], mask[4], gateway[4];
    uint16_t vendor_id, device_id, ip_info;
    uint8_t fields, role;
    bool duplicate_name, duplicate_ip, text_limited;
} dcp_device_t;

typedef enum {
    DCP_UNAVAILABLE, DCP_IDLE, DCP_STARTING, DCP_SCANNING, DCP_COMPLETE,
    DCP_CANCELLED, DCP_LINK_LOST, DCP_TX_FAILED, DCP_NETWORK_CHANGED
} dcp_state_t;

typedef struct {
    dcp_state_t state;
    uint32_t xid, overflow, malformed, queue_drops;
    int error_code;
    uint64_t started_ms, deadline_ms, finished_ms;
    unsigned count;
    bool valid, duplicate_name, duplicate_ip, reported_conflict, text_limited;
} dcp_summary_t;

typedef struct {
    dcp_summary_t summary;
    dcp_device_t devices[DCP_CAPACITY];
    uint32_t name_matches[DCP_CAPACITY], ip_matches[DCP_CAPACITY];
} dcp_inventory_t;

typedef enum { DCP_IGNORE, DCP_MALFORMED, DCP_PARSED } dcp_parse_t;

void dcp_identify_all(uint8_t frame[DCP_REQUEST_SIZE], const uint8_t mac[6], uint32_t xid);
bool dcp_matches(const uint8_t *frame, size_t length, const uint8_t mac[6], uint32_t xid);
dcp_parse_t dcp_parse(const uint8_t *frame, size_t length, const uint8_t mac[6],
                      uint32_t xid, dcp_device_t *device);
bool dcp_active(dcp_state_t state);
const char *dcp_state_name(dcp_state_t state);
bool dcp_partial(const dcp_summary_t *summary);
const char *dcp_ip_status_name(uint16_t status);
void dcp_begin(dcp_inventory_t *inventory, uint32_t xid, uint64_t now);
void dcp_finish(dcp_summary_t *summary, dcp_state_t state, uint64_t now);
void dcp_link_down(dcp_summary_t *summary, uint64_t now);
void dcp_observe(dcp_inventory_t *inventory, const dcp_device_t *device);
