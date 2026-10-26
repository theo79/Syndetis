#include "ping_packet.h"
#include <string.h>

static uint16_t word(const uint8_t *p) { return (uint16_t)p[0] << 8 | p[1]; }
static uint32_t address(const uint8_t *p) { return (uint32_t)word(p) << 16 | word(p + 2); }
static uint16_t checksum(const uint8_t *p, size_t n)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < n; i += 2) sum += (uint16_t)p[i] << 8 | (i + 1 < n ? p[i + 1] : 0);
    while (sum >> 16) sum = (sum & 65535) + (sum >> 16);
    return (uint16_t)~sum;
}
void ping_packet_make(uint8_t out[PING_PACKET_BYTES], uint16_t id, uint16_t seq, uint64_t cookie)
{
    memset(out, 0, PING_PACKET_BYTES); out[0] = 8;
    out[4] = id >> 8; out[5] = id; out[6] = seq >> 8; out[7] = seq;
    memcpy(out + 8, "SYNP", 4);
    for (unsigned i = 0; i < 8; ++i) out[12 + i] = cookie >> (56 - i * 8);
    uint16_t check = checksum(out, PING_PACKET_BYTES); out[2] = check >> 8; out[3] = check;
}
bool ping_packet_match(const uint8_t *ip, size_t n, uint32_t src, uint32_t dst,
                       uint16_t id, uint16_t seq, uint64_t cookie, bool reply)
{
    if (!ip || n < 20 || (ip[0] >> 4) != 4) return false;
    size_t ihl = (ip[0] & 15u) * 4u;
    if (ihl < 20 || ihl > 60 || n < ihl + PING_PACKET_BYTES || word(ip + 2) != ihl + PING_PACKET_BYTES ||
        ip[9] != 1 || (word(ip + 6) & 0x3fff) || address(ip + 12) != src || address(ip + 16) != dst ||
        checksum(ip, ihl)) return false;
    const uint8_t *icmp = ip + ihl;
    uint8_t expected[PING_PACKET_BYTES]; ping_packet_make(expected, id, seq, cookie);
    return icmp[0] == (reply ? 0 : 8) && icmp[1] == 0 && !checksum(icmp, PING_PACKET_BYTES) &&
           !memcmp(icmp + 4, expected + 4, PING_PACKET_BYTES - 4);
}
bool ping_frame_match(const uint8_t *f, size_t n, uint32_t src, uint32_t dst,
                      uint16_t id, uint16_t seq, uint64_t cookie, bool reply)
{
    return f && n >= 14 && f[12] == 8 && f[13] == 0 &&
           ping_packet_match(f + 14, n - 14, src, dst, id, seq, cookie, reply);
}
