#include "switch_info_parse.h"
#include <stdio.h>
#include <string.h>

static const uint8_t lldp_destination[6] = {1, 0x80, 0xc2, 0, 0, 0x0e};
static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

bool switch_info_is_lldp(const uint8_t *f, size_t len)
{
    return f && len >= 14 && memcmp(f, lldp_destination, 6) == 0 && be16(f + 12) == 0x88cc;
}

static void printable(char *out, size_t capacity, const uint8_t *in, size_t length)
{
    size_t n = length < capacity - 1 ? length : capacity - 1;
    for (size_t i = 0; i < n; ++i) out[i] = in[i] >= 0x20 && in[i] <= 0x7e ? (char)in[i] : '?';
    out[n] = 0;
}

static bool identifier(char *out, const uint8_t *p, size_t len, bool chassis)
{
    if (len < 2 || len > SWITCH_INFO_RAW_ID_SIZE || p[0] < 1 || p[0] > 7) return false;
    unsigned mac_subtype = chassis ? 4 : 3, network_subtype = chassis ? 5 : 4;
    if (p[0] == mac_subtype) {
        if (len != 7) return false;
        snprintf(out, SWITCH_INFO_ID_SIZE, "%02X:%02X:%02X:%02X:%02X:%02X", p[1], p[2], p[3], p[4], p[5], p[6]);
    } else if (p[0] == network_subtype) {
        if (len < 3) return false;
        if (p[1] == 1) {
            if (len != 6) return false;
            snprintf(out, SWITCH_INFO_ID_SIZE, "%u.%u.%u.%u", p[2], p[3], p[4], p[5]);
        } else if (p[1] == 2) {
            if (len != 18) return false;
            snprintf(out, SWITCH_INFO_ID_SIZE, "%x:%x:%x:%x:%x:%x:%x:%x",
                     be16(p+2), be16(p+4), be16(p+6), be16(p+8), be16(p+10), be16(p+12), be16(p+14), be16(p+16));
        } else {
            /* Preserve unknown address families as bounded hexadecimal. */
            size_t used = (size_t)snprintf(out, SWITCH_INFO_ID_SIZE, "AF%u:", p[1]);
            for (size_t i = 2; i < len && used + 2 < SWITCH_INFO_ID_SIZE; ++i)
                used += (size_t)snprintf(out+used, SWITCH_INFO_ID_SIZE-used, "%02X", p[i]);
        }
    } else printable(out, SWITCH_INFO_ID_SIZE, p+1, len-1);
    return true;
}

static bool management(const uint8_t *p, size_t len, switch_neighbor_t *n)
{
    if (len < 9) return false;
    size_t addr_len = p[0]; /* family octet included */
    if (addr_len < 2 || addr_len > 32 || len < 1 + addr_len + 6) return false;
    size_t oid_offset = 1 + addr_len + 5;
    if (p[1 + addr_len] < 1 || p[1 + addr_len] > 3 || p[oid_offset] > 128 ||
        len != oid_offset + 1 + p[oid_offset]) return false;
    if (p[1] == 1) {
        if (addr_len != 5) return false;
        if (!n->management_ipv4_present) {
            memcpy(n->management_ipv4, p+2, 4);
            n->management_ipv4_present = true;
        }
    }
    return true; /* Unsupported optional address families are safely ignored. */
}

bool switch_info_parse_lldp(const uint8_t *f, size_t len, switch_neighbor_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!switch_info_is_lldp(f, len) || len > SWITCH_INFO_FRAME_SIZE) return false;
    size_t pos = 14;
    unsigned mandatory = 1, seen = 0;
    bool ended = false;
    for (unsigned count = 0; count < SWITCH_INFO_MAX_TLVS; ++count) {
        if (len - pos < 2) goto malformed;
        uint16_t header = be16(f+pos);
        unsigned type = header >> 9;
        size_t size = header & 0x1ff;
        pos += 2;
        if (size > len-pos) goto malformed;
        const uint8_t *p = f+pos;
        if (mandatory <= 3 && type != mandatory) goto malformed;
        if (type == 0) {
            if (size != 0 || mandatory != 4) goto malformed;
            ended = true;
            break; /* Ethernet padding after End is not TLVs. */
        }
        if (type <= 7) {
            if (seen & (1u << type)) goto malformed;
            seen |= 1u << type;
        }
        switch (type) {
        case 1:
            if (!identifier(out->chassis_id, p, size, true)) goto malformed;
            memcpy(out->chassis_raw, p, size); out->chassis_raw_len = (uint16_t)size;
            ++mandatory; break;
        case 2:
            if (!identifier(out->port_id, p, size, false)) goto malformed;
            memcpy(out->port_raw, p, size); out->port_raw_len = (uint16_t)size;
            ++mandatory; break;
        case 3:
            if (size != 2) goto malformed;
            out->ttl = be16(p); ++mandatory; break;
        case 4:
            if (size > 255) goto malformed;
            printable(out->port_description, sizeof(out->port_description), p, size); break;
        case 5:
            if (size > 255) goto malformed;
            printable(out->system_name, sizeof(out->system_name), p, size); break;
        case 6:
            if (size > 255) goto malformed;
            printable(out->system_description, sizeof(out->system_description), p, size); break;
        case 7:
            if (size != 4) goto malformed;
            out->supported_capabilities = be16(p); out->enabled_capabilities = be16(p+2);
            if (out->enabled_capabilities & ~out->supported_capabilities) goto malformed;
            out->capabilities_present = true; break;
        case 8:
            if (!management(p, size, out)) goto malformed;
            break;
        case 127:
            if (size < 4) goto malformed;
            if (p[0] == 0 && p[1] == 0x80 && p[2] == 0xc2 && p[3] == 1) {
                if (size != 6 || be16(p+4) > 4094) goto malformed;
                out->pvid = be16(p+4); out->pvid_present = true;
            }
            break; /* Unknown OUI/subtype safely skipped. */
        default: break;
        }
        pos += size;
    }
    if (!ended) goto malformed;
    memcpy(out->source_mac, f+6, 6);
    out->protocol = SWITCH_PROTOCOL_LLDP;
    out->valid = out->ttl != 0;
    return true;
malformed:
    memset(out, 0, sizeof(*out));
    return false;
}

bool switch_info_same_neighbor(const switch_neighbor_t *a, const switch_neighbor_t *b)
{
    return a->protocol == b->protocol && a->chassis_raw_len && a->port_raw_len &&
           a->chassis_raw_len <= SWITCH_INFO_RAW_ID_SIZE && a->port_raw_len <= SWITCH_INFO_RAW_ID_SIZE &&
           a->chassis_raw_len == b->chassis_raw_len && a->port_raw_len == b->port_raw_len &&
           memcmp(a->chassis_raw, b->chassis_raw, a->chassis_raw_len) == 0 &&
           memcmp(a->port_raw, b->port_raw, a->port_raw_len) == 0;
}

bool switch_info_neighbor_current(const switch_neighbor_t *n, bool up, uint32_t gen, int64_t now)
{
    return n->valid && up && n->link_generation == gen && now < n->expiration_us;
}
