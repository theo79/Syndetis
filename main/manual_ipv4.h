#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// All configuration arithmetic is host order; no persistence or inferred values.
typedef struct { uint32_t ip, gateway; uint8_t prefix; } manual_ipv4_t;
uint32_t manual_ipv4_mask(unsigned prefix);
const char *manual_ipv4_validate(const manual_ipv4_t *config);
void manual_ipv4_arp(uint8_t frame[60], const uint8_t mac[6], uint32_t ip, bool announce);
bool manual_ipv4_conflict(const uint8_t *frame, size_t length, const uint8_t mac[6],
                          uint32_t ip, bool probing);
