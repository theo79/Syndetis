#pragma once
#include <stdint.h>
#define DNS_MAX_SERVERS 3
typedef struct { uint32_t addr; } ip_addr_t;
const ip_addr_t *dns_getserver(int slot);
void dns_setserver(int slot, const ip_addr_t *value);
