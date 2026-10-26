#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Network-order values converted to host order by the caller.
bool dhcp_management_overlap(uint32_t ip, uint32_t mask);
// Baseline ARP/DHCP classifier. The address owner separately admits only the
// currently expected diagnostic Echo Reply through its generation/cookie gate.
// In particular ALL TCP (even to the AP address) is rejected at Ethernet ingress.
bool dhcp_stack_frame(const uint8_t *frame, size_t length);
bool dhcp_browser_request(const char *host, const char *origin,
                          const char *content_type, size_t content_length);
