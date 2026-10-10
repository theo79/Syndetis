#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { AT_PASS, AT_WARN, AT_FAIL, AT_SKIP } autotest_grade_t;
/* Pure expression rules: used by firmware and compile-time policy tests. */
#define AT_LINK(up) ((up) ? AT_PASS : AT_FAIL)
#define AT_NEGOTIATION(known, full) (!(known) || !(full) ? AT_WARN : AT_PASS)
#define AT_DHCP(link, valid_ip, client) (!(link) ? AT_SKIP : !(valid_ip) ? AT_FAIL : (client) ? AT_PASS : AT_WARN)
#define AT_GATEWAY(configured, sent, received) (!(configured) ? AT_WARN : !(sent) || !(received) ? AT_FAIL : (received) < (sent) ? AT_WARN : AT_PASS)
#define AT_DNS(configured) ((configured) ? AT_PASS : AT_WARN)
#define AT_MERGE(a, b) ((a) == AT_FAIL || (b) == AT_FAIL ? AT_FAIL : (a) == AT_WARN || (b) == AT_WARN ? AT_WARN : AT_PASS)
/* IPv4 inputs are host-order integers. /31 and /32 support is intentional. */
#define AT_MASK_VALID(mask) ((mask) != 0 && (((~(uint32_t)(mask)) & ((~(uint32_t)(mask)) + 1U)) == 0))
#define AT_UNICAST(ip) ((ip) != 0 && ((uint32_t)(ip) >> 24) != 0 && ((uint32_t)(ip) >> 24) != 127 && ((uint32_t)(ip) >> 24) < 224)
#define AT_IP_VALID(ip, mask) (AT_UNICAST(ip) && AT_MASK_VALID(mask) && ((uint32_t)(mask) >= 0xfffffffeU || (((uint32_t)(ip) & ~(uint32_t)(mask)) != 0 && ((uint32_t)(ip) & ~(uint32_t)(mask)) != ~(uint32_t)(mask))))

#define AT_GATEWAY_VALID(ip, mask, gw) (!(gw) || (AT_UNICAST(gw) && ((mask) == UINT32_MAX || (((gw) & (mask)) == ((ip) & (mask)) && AT_IP_VALID(gw, mask) && (gw) != (ip)))))
#define AT_RESOLUTION(success) ((success) ? AT_PASS : AT_WARN)
