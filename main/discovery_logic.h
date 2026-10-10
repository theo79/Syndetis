#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define DISCOVERY_MAX_HOSTS 254
#ifdef __cplusplus
#define DISC_INLINE static inline constexpr
#else
#define DISC_INLINE static inline
#endif
/* All addresses in this module are numeric, host byte order. /31 and /32
 * have no candidates: this release always excludes network and broadcast. */
typedef struct {
    uint32_t own, mask, network, broadcast, first, last, usable, candidates;
    unsigned prefix;
} discovery_subnet_t;
DISC_INLINE bool discovery_subnet(uint32_t ip, uint32_t mask, discovery_subnet_t *s)
{
    if (!mask || ((~mask) & ((~mask) + 1U)) || !ip ||
        (ip >> 24) == 0 || (ip >> 24) == 127 || (ip >> 24) >= 224) return false;
    *s = (discovery_subnet_t){.own=ip, .mask=mask, .network=ip & mask, .broadcast=ip | ~mask};
    for (uint32_t m=mask; m; m <<= 1) ++s->prefix;
    s->usable = ~mask > 1 ? ~mask - 1 : 0;
    s->first = s->usable ? s->network + 1 : 0;
    s->last = s->usable ? s->broadcast - 1 : 0;
    if (s->usable && (ip == s->network || ip == s->broadcast)) return false;
    s->candidates = s->usable ? s->usable - 1 : 0;
    return true;
}
DISC_INLINE bool discovery_allowed(const discovery_subnet_t *s) { return s->usable <= DISCOVERY_MAX_HOSTS; }
DISC_INLINE bool discovery_candidate(const discovery_subnet_t *s, uint32_t ip)
{ return s->usable && ip >= s->first && ip <= s->last && ip != s->own; }
DISC_INLINE bool discovery_present(bool arp, bool ping) { return arp || ping; }
DISC_INLINE int discovery_ip_compare(uint32_t a, uint32_t b) { return (a > b) - (a < b); }

/* Shared by firmware and deterministic host simulation. */
#define DISCOVERY_ARP_REQUESTS_PER_SECOND 25
#define DISCOVERY_ARP_STEP_MS (1000 / DISCOVERY_ARP_REQUESTS_PER_SECOND)
#define DISCOVERY_ARP_SETTLE_MS 500
#define DISCOVERY_ICMP_CONCURRENCY 4
#define DISCOVERY_ICMP_TIMEOUT_MS 250
typedef struct {
    /* Existing field names remain compatible with Fast Discover clients. */
    uint32_t ip, rtt_ms;
    uint32_t last_seen_ms, flags;
    char vendor[32], hostname[64];
    uint8_t discovery_source, packet_loss;
    bool valid, vendor_present, hostname_present, locally_administered;
    bool ping_measured;
    uint8_t mac[6];
    bool mac_valid, arp_present, icmp_responsive;
} discovery_device_t;
DISC_INLINE void discovery_observe(discovery_device_t *table, uint32_t *count,
                                    uint32_t ip, const uint8_t *mac)
{
    if (!table || !count || *count>DISCOVERY_MAX_HOSTS) return;
    unsigned slot=0;
    while (slot<*count && table[slot].ip!=ip) ++slot;
    if (slot>=DISCOVERY_MAX_HOSTS) return;
    if (slot==*count) table[(*count)++]=(discovery_device_t){.ip=ip};
    table[slot].valid=table[slot].arp_present=true;
    table[slot].discovery_source=1; /* ARP; reserved values for future sources. */
    if (!mac) return;
    bool changed=!table[slot].mac_valid;
    for (unsigned i=0;i<6;++i) changed=changed || table[slot].mac[i]!=mac[i];
    if (changed) { table[slot].vendor_present=false; table[slot].vendor[0]=0; }
    table[slot].mac_valid=true;
    table[slot].locally_administered=(mac[0]&2)!=0;
    for (unsigned i=0;i<6;++i) table[slot].mac[i]=mac[i];
}
DISC_INLINE bool discovery_observation_allowed(const discovery_subnet_t *subnet,
                                                const bool *issued,uint32_t ip)
{
    return discovery_candidate(subnet,ip) && issued[ip-subnet->first];
}
#undef DISC_INLINE
