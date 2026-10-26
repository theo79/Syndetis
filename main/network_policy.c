#include "network_policy.h"
#include <string.h>

bool dhcp_management_overlap(uint32_t ip, uint32_t mask)
{
    // CIDR ranges overlap when either contains the other's network address.
    return (ip & 0xffffff00u) == 0xc0a80400u ||
           (ip & mask) == (0xc0a80400u & mask);
}

bool dhcp_stack_frame(const uint8_t *f, size_t n)
{
    if (!f || n < 14) return false;
    unsigned type = (unsigned)f[12] << 8 | f[13];
    if (type == 0x0806) return n >= 42; // ARP; stack validates its fields.
    // Tagged discovery remains supported; tagged IP is deliberately not a new feature.
    if (type != 0x0800 || n < 34 || (f[14] >> 4) != 4) return false;
    size_t ihl = (f[14] & 15u) * 4u;
    if (ihl < 20 || n < 14 + ihl + 8 || f[23] != 17) return false;
    if ((f[20] & 0x3f) || f[21]) return false; // Reject MF and fragment offsets.
    const uint8_t *udp = f + 14 + ihl;
    return udp[0] == 0 && udp[1] == 67 && udp[2] == 0 && udp[3] == 68;
}

bool dhcp_browser_request(const char *host, const char *origin,
                          const char *type, size_t length)
{
    return host && origin && type && !strcmp(host, "192.168.4.1") &&
        !strcmp(origin, "http://192.168.4.1") &&
        !strcmp(type, "application/json") && length == 2;
}
