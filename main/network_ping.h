#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    bool active, valid;
    uint32_t target;
    unsigned sent, replies, completed, generation;
    uint64_t rtt_sum_us;
    int error;
    const char *detail;
} network_ping_status_t;
// NULL means accepted. No socket/stack calls execute in the UI.
const char *network_ping_start(bool gateway, uint32_t custom);
void network_ping_cancel(void);
network_ping_status_t network_ping_status(void);
// Called only by the existing addressing worker; bounded nonblocking socket work.
void network_ping_step(void);
