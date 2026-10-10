#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define PTR_DNS_PACKET_SIZE 512
typedef enum { PTR_IGNORE, PTR_FOUND, PTR_UNRESOLVED } ptr_dns_result_t;
/* Isolated minimal unicast IPv4 PTR wire codec. Installed lwIP only resolves
 * A/AAAA. No SDK patch, multicast, caching, retries or general DNS resolver. */
size_t ptr_dns_query(uint8_t *out, size_t capacity, uint16_t id, uint32_t ip);
ptr_dns_result_t ptr_dns_response(const uint8_t *packet, size_t length,
                                uint16_t id, uint32_t ip, char hostname[64]);
