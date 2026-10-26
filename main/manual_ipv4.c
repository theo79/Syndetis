#include "manual_ipv4.h"
#include "network_policy.h"
#include <string.h>

uint32_t manual_ipv4_mask(unsigned prefix)
{
    return prefix >= 1 && prefix <= 30 ? UINT32_MAX << (32 - prefix) : 0;
}

static bool host_address(uint32_t ip, uint32_t mask)
{
    uint32_t host = ~mask;
    return (ip >> 24) != 0 && (ip >> 24) != 127 && ip < 0xe0000000u &&
           (ip & host) != 0 && (ip & host) != host;
}

const char *manual_ipv4_validate(const manual_ipv4_t *c)
{
    if (!c) return "Invalid settings";
    uint32_t mask = manual_ipv4_mask(c->prefix);
    if (!mask) return "Prefix must be 1-30";
    if (!host_address(c->ip, mask)) return "Invalid host IP";
    if (dhcp_management_overlap(c->ip, mask)) return "Overlaps hotspot";
    if (c->gateway && (!host_address(c->gateway, mask) || c->gateway == c->ip ||
                      (c->gateway & mask) != (c->ip & mask))) return "Invalid gateway";
    return NULL;
}

static void put_ip(uint8_t *p, uint32_t ip)
{
    p[0] = ip >> 24; p[1] = ip >> 16; p[2] = ip >> 8; p[3] = ip;
}

static uint32_t get_ip(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

void manual_ipv4_arp(uint8_t f[60], const uint8_t mac[6], uint32_t ip, bool announce)
{
    memset(f, 0, 60); // Ethernet minimum payload, no FCS (driver supplies it).
    memset(f, 0xff, 6); memcpy(f + 6, mac, 6);
    f[12] = 8; f[13] = 6; f[15] = 1; f[16] = 8;
    f[18] = 6; f[19] = 4; f[21] = 1; // Ethernet/IPv4 ARP Request.
    memcpy(f + 22, mac, 6);
    if (announce) put_ip(f + 28, ip);
    put_ip(f + 38, ip);
}

bool manual_ipv4_conflict(const uint8_t *f, size_t n, const uint8_t mac[6],
                          uint32_t ip, bool probing)
{
    if (!f || n < 42 || f[12] != 8 || f[13] != 6 || f[14] || f[15] != 1 ||
        f[16] != 8 || f[17] || f[18] != 6 || f[19] != 4 || f[20] ||
        (f[21] != 1 && f[21] != 2) || !memcmp(f + 22, mac, 6)) return false;
    uint32_t sender = get_ip(f + 28);
    return sender == ip || (probing && f[21] == 1 && !sender && get_ip(f + 38) == ip);
}
