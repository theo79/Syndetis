#include "neighbors.h"
#include <stdio.h>
#include <string.h>

static const uint8_t cdp_destination[6] = {1,0,12,204,204,204};
static const uint8_t cdp_snap[8] = {0xaa,0xaa,3,0,0,12,0x20,0};
static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)be16(p) << 16) | be16(p + 2); }

bool neighbor_destination(const uint8_t *p, size_t n)
{
    if (n < 14) return false;
    return memcmp(p, cdp_destination, 6) == 0 ||
        (p[0] == 1 && p[1] == 0x80 && p[2] == 0xc2 && !p[3] && !p[4] &&
         (p[5] == 0x0e || p[5] == 3 || p[5] == 0));
}

static void text(char *out, const uint8_t *in, size_t n, neighbor_t *r)
{
    for (size_t i = 0; i < n; ++i) {
        bool printable = in[i] >= 32 && in[i] <= 126;
        out[i] = printable ? in[i] : '?';
        if (!printable) r->sanitized = true;
    }
    out[n] = 0;
}

static void ip_text(char *out, const uint8_t *p, bool ipv6)
{
    if (!ipv6) snprintf(out, 48, "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
    else snprintf(out, 48, "%x:%x:%x:%x:%x:%x:%x:%x", be16(p), be16(p+2), be16(p+4),
                  be16(p+6), be16(p+8), be16(p+10), be16(p+12), be16(p+14));
}

static bool identifier(char *out, const uint8_t *p, size_t n, int subtype,
                       bool chassis, neighbor_t *r)
{
    if (!n || n > 255 || subtype < 1 || subtype > 7) return false;
    if (subtype == (chassis ? 4 : 3)) {
        if (n != 6) return false;
        snprintf(out, 512, "%02X:%02X:%02X:%02X:%02X:%02X", p[0], p[1], p[2], p[3], p[4], p[5]);
    } else if (subtype == (chassis ? 5 : 4)) {
        if (n == 5 && p[0] == 1) ip_text(out, p + 1, false);
        else if (n == 17 && p[0] == 2) ip_text(out, p + 1, true);
        else {
            // Unknown network address family: keep exact bytes as hex.
            for (size_t i = 0; i < n; ++i) snprintf(out + 2*i, 3, "%02X", p[i]);
        }
    } else text(out, p, n, r);
    return true;
}

static bool lldp_management(const uint8_t *v, size_t n, neighbor_t *r)
{
    if (n < 1) return false;
    size_t a = v[0]; // Address-string length includes its address-family byte.
    if (a < 2 || a > 31 || n < a + 7 || v[a + 6] > 128 || n != a + 7 + v[a + 6]) return false;
    bool ipv4 = v[1] == 1, ipv6 = v[1] == 2;
    if ((ipv4 && a != 5) || (ipv6 && a != 17)) return false;
    if ((ipv4 || ipv6) && (!(r->fields & FIELD_MANAGEMENT) ||
                            (ipv4 && strchr(r->management, ':')))) {
        ip_text(r->management, v + 2, ipv6);
        r->fields |= FIELD_MANAGEMENT;
        r->management_kind = 1;
    }
    return true;
}

static parse_result_t parse_lldp(const uint8_t *p, size_t n, neighbor_t *r)
{
    unsigned mandatory = 1;
    while (n >= 2) {
        unsigned type = p[0] >> 1;
        size_t len = ((p[0] & 1) << 8) | p[1];
        p += 2; n -= 2;
        if (len > n) return PARSE_INVALID;
        if (mandatory <= 3 && type != mandatory) return PARSE_INVALID;
        if (type == 0) return !len && mandatory == 4 ? PARSE_VALID : PARSE_INVALID;
        if (type <= 3) {
            if (type != mandatory++) return PARSE_INVALID;
            if (type == 3) {
                if (len != 2) return PARSE_INVALID;
                r->ttl = be16(p);
            } else {
                if (len < 2 || len > 256) return PARSE_INVALID;
                if (type == 1) {
                    r->chassis_subtype = p[0]; r->chassis_length = len - 1;
                    memcpy(r->chassis_key, p + 1, len - 1);
                    if (!identifier(r->chassis, p + 1, len - 1, p[0], true, r)) return PARSE_INVALID;
                    r->fields |= FIELD_CHASSIS;
                } else {
                    r->port_subtype = p[0]; r->port_length = len - 1;
                    memcpy(r->port_key, p + 1, len - 1);
                    if (!identifier(r->port, p + 1, len - 1, p[0], false, r)) return PARSE_INVALID;
                    r->fields |= FIELD_PORT;
                }
            }
        } else if (type == 4 || type == 5) {
            unsigned field = type == 4 ? FIELD_DESCRIPTION : FIELD_NAME;
            if (len > 255) return PARSE_LIMIT;
            if (r->fields & field) return PARSE_INVALID;
            text(type == 4 ? r->description : r->name, p, len, r);
            r->fields |= field;
        } else if (type == 8) {
            if (!lldp_management(p, len, r)) return PARSE_INVALID;
        } else if (type == 127) {
            if (len < 4) return PARSE_INVALID;
            if (p[0] == 0 && p[1] == 0x80 && p[2] == 0xc2 && p[3] == 1) {
                if (len != 6 || (r->fields & FIELD_VLAN) || be16(p + 4) > 4094) return PARSE_INVALID;
                r->vlan = be16(p + 4); r->fields |= FIELD_VLAN;
            }
        }
        p += len; n -= len;
    }
    return PARSE_INVALID; // End-of-LLDPDU is required, not an optional truncation.
}

static bool cdp_checksum_valid(const uint8_t *p, size_t n)
{
    uint32_t sum = 0;
    while (n > 1) { sum += be16(p); p += 2; n -= 2; }
    if (n) {
        // Cisco's odd final byte uses a sign-extended low word, with correction.
        sum += p[0] & 0x80 ? 0xff00U + p[0] - 1 : p[0];
    }
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return sum == 0xffff;
}

static bool cdp_addresses(const uint8_t *p, size_t n, bool management, neighbor_t *r)
{
    if (n < 4) return false;
    uint32_t count = be32(p); p += 4; n -= 4;
    if (count > n / 5) return false;
    for (uint32_t i = 0; i < count; ++i) {
        if (n < 2) return false;
        uint8_t type = p[0], proto_len = p[1];
        p += 2; n -= 2;
        if (!proto_len || n < (size_t)proto_len + 2) return false;
        bool ipv4 = type == 1 && proto_len == 1 && p[0] == 0xcc;
        const uint8_t ipv6_proto[8] = {0xaa,0xaa,3,0,0,0,0x86,0xdd};
        bool ipv6 = type == 2 && proto_len == 8 && memcmp(p, ipv6_proto, 8) == 0;
        p += proto_len; n -= proto_len;
        size_t len = be16(p); p += 2; n -= 2;
        if (len > n || (ipv4 && len != 4) || (ipv6 && len != 16)) return false;
        if ((ipv4 || ipv6) && (!(r->fields & FIELD_MANAGEMENT) ||
             (management && r->management_kind == 2) ||
             (r->management_kind == (management ? 1 : 2) && ipv4 && strchr(r->management, ':')))) {
            ip_text(r->management, p, ipv6);
            r->management_kind = management ? 1 : 2;
            r->fields |= FIELD_MANAGEMENT;
        }
        p += len; n -= len;
    }
    return n == 0;
}

static parse_result_t parse_cdp(const uint8_t *p, size_t n, neighbor_t *r)
{
    if (n < 4 || (p[0] != 1 && p[0] != 2) || !cdp_checksum_valid(p, n)) return PARSE_INVALID;
    r->ttl = p[1];
    p += 4; n -= 4;
    bool device = false, port = false, management = false, addresses = false;
    while (n) {
        if (n < 4) return PARSE_INVALID;
        unsigned type = be16(p);
        size_t len = be16(p + 2);
        if (len < 4 || len > n) return PARSE_INVALID;
        const uint8_t *v = p + 4; size_t value_len = len - 4;
        if (type == 1 || type == 3) {
            if (!value_len) return PARSE_INVALID;
            if (value_len > 255) return PARSE_LIMIT;
            if (type == 1) {
                if (device) return PARSE_INVALID;
                device = true; r->chassis_length = value_len;
                memcpy(r->chassis_key, v, value_len);
                text(r->name, v, value_len, r); r->fields |= FIELD_NAME;
            } else {
                if (port) return PARSE_INVALID;
                port = true; r->port_length = value_len;
                memcpy(r->port_key, v, value_len);
                text(r->port, v, value_len, r); r->fields |= FIELD_PORT;
            }
        } else if (type == 2 || type == 0x16) {
            bool *seen = type == 2 ? &addresses : &management;
            if (*seen || !cdp_addresses(v, value_len, type == 0x16, r)) return PARSE_INVALID;
            *seen = true;
        } else if (type == 0x0a) {
            if (value_len != 2 || (r->fields & FIELD_VLAN) || be16(v) > 4094) return PARSE_INVALID;
            r->vlan = be16(v); r->fields |= FIELD_VLAN;
        }
        p += len; n -= len;
    }
    return device && port ? PARSE_VALID : PARSE_INVALID;
}

parse_result_t neighbor_parse(const uint8_t *frame, size_t n, neighbor_t *r)
{
    if (!frame || !r || !neighbor_destination(frame, n)) return PARSE_IGNORE;
    if (n > NEIGHBOR_FRAME_MAX) return PARSE_LIMIT;
    memset(r, 0, sizeof(*r));
    if ((frame[6] & 1) || !memcmp(frame + 6, "\0\0\0\0\0\0", 6)) return PARSE_INVALID;
    memcpy(r->source, frame + 6, 6);
    uint16_t type = be16(frame + 12); size_t offset = 14;
    if (type == 0x8100) {
        if (n < 18) return PARSE_INVALID;
        type = be16(frame + 16); offset = 18;
    }
    if (type == 0x8100 || type == 0x88a8) return PARSE_LIMIT; // Single 802.1Q only.
    parse_result_t result;
    if (type == 0x88cc && memcmp(frame, cdp_destination, 6)) {
        r->protocol = PROTOCOL_LLDP;
        result = parse_lldp(frame + offset, n - offset, r);
    } else if (type <= 1500 && !memcmp(frame, cdp_destination, 6)) {
        if (type < 12 || type > n - offset || memcmp(frame + offset, cdp_snap, 8)) return PARSE_INVALID;
        r->protocol = PROTOCOL_CDP;
        result = parse_cdp(frame + offset + 8, type - 8, r);
    } else return PARSE_IGNORE;
    r->used = result == PARSE_VALID;
    return result;
}

bool neighbor_expired(const neighbor_t *r, uint64_t now)
{
    return r->used && now >= r->received_ms && now - r->received_ms >= (uint64_t)r->ttl * 1000;
}

void neighbors_link(neighbor_table_t *t, bool up, uint64_t now)
{
    if (up && t->link_up) return;
    uint32_t next = t->session + 1;
    memset(t, 0, sizeof(*t));
    t->session = next; t->link_up = up; t->link_since_ms = now;
}

static bool same_identity(const neighbor_t *a, const neighbor_t *b)
{
    return a->protocol == b->protocol && !memcmp(a->source, b->source, 6) &&
        a->chassis_subtype == b->chassis_subtype && a->port_subtype == b->port_subtype &&
        a->chassis_length == b->chassis_length && a->port_length == b->port_length &&
        !memcmp(a->chassis_key, b->chassis_key, a->chassis_length) &&
        !memcmp(a->port_key, b->port_key, a->port_length);
}

bool neighbors_accept(neighbor_table_t *t, const neighbor_t *r, uint32_t session,
                      uint64_t received, uint64_t now)
{
    if (!t->link_up || session != t->session || !r->used) return false;
    int match = -1, available = -1;
    for (int i = 0; i < NEIGHBOR_CAPACITY; ++i) {
        if (t->records[i].used && same_identity(&t->records[i], r)) match = i;
        if (available < 0 && (!t->records[i].used || neighbor_expired(&t->records[i], now))) available = i;
    }
    if (!r->ttl) { // Withdrawal: delete matching information, do not add a neighbor.
        t->ever_received = true;
        if (match >= 0) memset(&t->records[match], 0, sizeof(neighbor_t));
        return match >= 0;
    }
    if (now < received || now - received >= (uint64_t)r->ttl * 1000) return false;
    t->ever_received = true;
    if (match < 0) match = available;
    if (match < 0) { ++t->capacity_drops; return false; }
    t->records[match] = *r; t->records[match].received_ms = received;
    return true;
}

discovery_state_t neighbors_state(const neighbor_table_t *t, uint64_t now)
{
    if (!t->link_up) return DISC_LINK_DOWN;
    for (int i = 0; i < NEIGHBOR_CAPACITY; ++i)
        if (t->records[i].used && !neighbor_expired(&t->records[i], now)) return DISC_CURRENT;
    if (t->ever_received) return DISC_EXPIRED;
    return now - t->link_since_ms >= NEIGHBOR_OBSERVE_MS ? DISC_NONE : DISC_WAITING;
}

const char *neighbors_state_name(discovery_state_t state)
{
    const char *names[] = {"Link down", "Waiting for advertisement", "No advertisement received", "Received/current", "Expired"};
    return names[state];
}
