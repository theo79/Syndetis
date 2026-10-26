#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Host-order arithmetic adapted from the preserved A-G target selector.
static inline uint32_t ping_ipv4(const char *s)
{
    uint32_t ip = 0;
    for (int i = 0; i < 4; ++i) {
        unsigned value = 0, digits = 0;
        while (*s >= '0' && *s <= '9' && digits < 3) { value = value * 10 + *s++ - '0'; ++digits; }
        if (!digits || value > 255 || (i < 3 ? *s++ != '.' : *s != 0)) return 0;
        ip = (ip << 8) | value;
    }
    return ip;
}

static inline const char *ping_target_validate(uint32_t target, uint32_t local, uint32_t mask, uint32_t gateway)
{
    if (!local || !mask || ((~mask) & ((~mask) + 1))) return "No usable wired IP";
    if ((target >> 24) == 0 || (target >> 24) == 127 || target >= 0xe0000000u) return "Invalid target IP";
    if (target == local) return "Target is YOUR IP";
    if ((target & 0xffffff00u) == 0xc0a80400u) return "Management subnet";
    uint32_t host = ~mask;
    if ((local >> 24) == 0 || (local >> 24) == 127 || local >= 0xe0000000u ||
        (host > 1 && (!(local & host) || (local & host) == host))) return "No usable wired IP";
    bool on_link = (target & mask) == (local & mask);
    if (on_link && host > 1 && (!(target & host) || (target & host) == host)) return "Network/broadcast IP";
    if (!on_link && !gateway) return "Gateway required";
    if (!on_link && ((gateway >> 24) == 0 || (gateway >> 24) == 127 || gateway >= 0xe0000000u ||
        gateway == local || (gateway & mask) != (local & mask) ||
        (host > 1 && (!(gateway & host) || (gateway & host) == host)))) return "Invalid gateway";
    return NULL;
}
