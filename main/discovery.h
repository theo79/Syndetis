#pragma once
#include "neighbors.h"
#include "esp_err.h"

typedef struct {
    bool available;
    discovery_state_t state;
    uint32_t capacity_drops, invalid_frames, limited_frames, queue_drops;
    uint8_t protocols[NEIGHBOR_CAPACITY];
    bool used[NEIGHBOR_CAPACITY], expired[NEIGHBOR_CAPACITY];
} discovery_summary_t;

esp_err_t discovery_init(void);
void discovery_link(bool up);
// Borrows the buffer, copying eligible discovery frames to the bounded queue.
// The single Ethernet dispatcher retains ownership.
void discovery_receive(const uint8_t *buffer, size_t length);
discovery_summary_t discovery_summary(void);
bool discovery_record(int index, neighbor_t *out);
bool discovery_snapshot(neighbor_table_t *out);
