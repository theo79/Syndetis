#include "switch_info_parse.h"
#include <string.h>

static const uint8_t cdp_destination[6] = {1, 0, 0x0c, 0xcc, 0xcc, 0xcc};
static const uint8_t cdp_snap[8] = {0xaa, 0xaa, 3, 0, 0, 0x0c, 0x20, 0};
static uint16_t cdp_be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t cdp_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

bool switch_info_is_cdp(const uint8_t *frame, size_t length)
{
    if (!frame || length < 26 || memcmp(frame, cdp_destination, 6)) return false;
    size_t declared = cdp_be16(frame + 12);
    return declared >= 12 && declared <= 1500 && declared <= length - 14 &&
           memcmp(frame + 14, cdp_snap, sizeof(cdp_snap)) == 0;
}

static bool cdp_checksum_valid(const uint8_t *p, size_t size)
{
    /* Includes CDP header/checksum and TLVs, excludes LLC and Ethernet padding.
     * Cisco's odd final byte is a low octet; >=128 also has the historical
     * signed-byte/off-by-one adjustment documented by Wireshark's CDP dissector.
     * Implement with unsigned arithmetic, no platform/endian assumptions. */
    uint32_t sum = 0;
    size_t i = 0;
    for (; i + 1 < size; i += 2) sum += cdp_be16(p + i);
    if (i < size) sum += p[i] < 128 ? p[i] : 0xff00U + p[i] - 1U;
    while (sum >> 16) sum = (sum & 0xffffU) + (sum >> 16);
    return sum == 0xffffU;
}

static void cdp_text(char *out, size_t capacity, const uint8_t *p, size_t size)
{
    size_t count = size < capacity - 1 ? size : capacity - 1;
    for (size_t i = 0; i < count; ++i) out[i] = p[i] >= 32 && p[i] <= 126 ? (char)p[i] : '?';
    out[count] = 0;
}

static bool cdp_addresses(const uint8_t *p, size_t size, switch_neighbor_t *out)
{
    if (size < 4) return false;
    uint32_t count = cdp_be32(p);
    size_t pos = 4;
    /* At least four envelope bytes per entry; also bound work independently. */
    if (count > SWITCH_INFO_MAX_TLVS || count > (size - 4) / 4) return false;
    for (uint32_t i = 0; i < count; ++i) {
        if (size - pos < 2) return false;
        unsigned protocol_type = p[pos++], protocol_size = p[pos++];
        if (protocol_size > size - pos || size - pos - protocol_size < 2) return false;
        const uint8_t *protocol = p + pos;
        pos += protocol_size;
        size_t address_size = cdp_be16(p + pos);
        pos += 2;
        if (address_size > size - pos) return false;
        bool ipv4 = protocol_type == 1 && protocol_size == 1 && protocol[0] == 0xcc;
        const uint8_t ipv4_snap[8] = {0xaa, 0xaa, 3, 0, 0, 0, 8, 0};
        if (protocol_type == 2 && protocol_size == 8 && !memcmp(protocol, ipv4_snap, 8)) ipv4 = true;
        if (ipv4) {
            if (address_size != 4) return false;
            if (!out->management_ipv4_present) {
                memcpy(out->management_ipv4, p + pos, 4);
                out->management_ipv4_present = true;
            }
        }
        pos += address_size; /* Unsupported bounded protocol/address skipped. */
    }
    return pos == size;
}

bool switch_info_parse_cdp(const uint8_t *frame, size_t length, switch_neighbor_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!switch_info_is_cdp(frame, length) || length > SWITCH_INFO_FRAME_SIZE) return false;
    size_t end = 14 + cdp_be16(frame + 12);
    if ((frame[22] != 1 && frame[22] != 2) || !cdp_checksum_valid(frame + 22, end - 22)) return false;
    out->cdp_version = frame[22]; out->ttl = frame[23];
    size_t pos = 26;
    uint32_t seen = 0;
    unsigned count = 0;
    while (pos < end) {
        if (++count > SWITCH_INFO_MAX_TLVS || end - pos < 4) goto malformed;
        unsigned type = cdp_be16(frame + pos);
        size_t size = cdp_be16(frame + pos + 2);
        if (size < 4 || size > end - pos) goto malformed;
        const uint8_t *p = frame + pos + 4;
        pos += size; size -= 4;
        /* Only parsed singleton fields are duplicate-rejected. Address and
         * management-address lists may repeat; retain first supported IPv4. */
        if (type == 1 || type == 3 || type == 4 || type == 5 || type == 6 ||
            type == 0x0a || type == 0x0b || type == 0x14) {
            if (seen & (1U << type)) goto malformed;
            seen |= 1U << type;
        }
        switch (type) {
        case 1:
            if (!size || size > SWITCH_INFO_RAW_ID_SIZE) goto malformed;
            cdp_text(out->device_id, sizeof(out->device_id), p, size);
            cdp_text(out->chassis_id, sizeof(out->chassis_id), p, size);
            memcpy(out->chassis_raw, p, size); out->chassis_raw_len = (uint16_t)size;
            break;
        case 3:
            if (!size || size > SWITCH_INFO_RAW_ID_SIZE) goto malformed;
            cdp_text(out->port_id, sizeof(out->port_id), p, size);
            memcpy(out->port_raw, p, size); out->port_raw_len = (uint16_t)size;
            break;
        case 2:
        case 0x16:
            if (!cdp_addresses(p, size, out)) goto malformed;
            break;
        case 4:
            if (size != 4) goto malformed;
            out->cdp_capabilities = cdp_be32(p); out->cdp_capabilities_present = true;
            break;
        case 5:
            cdp_text(out->system_description, sizeof(out->system_description), p, size); break;
        case 6:
            cdp_text(out->platform, sizeof(out->platform), p, size); break;
        case 0x14:
            cdp_text(out->system_name, sizeof(out->system_name), p, size); break;
        case 0x0a:
            if (size != 2 || cdp_be16(p) > 4094) goto malformed;
            out->pvid = cdp_be16(p); out->pvid_present = true; break;
        case 0x0b:
            if (size != 1 || p[0] > 1) goto malformed;
            out->duplex_present = true; out->full_duplex = p[0] == 1; break;
        default: break;
        }
    }
    if (!out->chassis_raw_len || !out->port_raw_len) goto malformed;
    if (!out->system_name[0]) cdp_text(out->system_name, sizeof(out->system_name), out->chassis_raw, out->chassis_raw_len);
    memcpy(out->source_mac, frame + 6, 6);
    out->protocol = SWITCH_PROTOCOL_CDP;
    out->valid = out->ttl != 0;
    return true;
malformed:
    memset(out, 0, sizeof(*out));
    return false;
}
