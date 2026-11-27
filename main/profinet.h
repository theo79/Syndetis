#pragma once
#include "profinet_dcp.h"
void profinet_init(void *driver, const uint8_t mac[6]);
void profinet_link(bool up);
void profinet_receive(const uint8_t *frame, size_t length);
const char *profinet_start(void);
void profinet_cancel(void);
dcp_summary_t profinet_summary(void);
bool profinet_device(uint32_t xid, unsigned index, dcp_device_t *device);
void profinet_snapshot(dcp_inventory_t *out);
