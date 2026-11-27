#include "profinet_dcp.h"
#include <string.h>

_Static_assert(DCP_CAPACITY <= 32, "Conflict masks require at most 32 devices");

static unsigned be16(const uint8_t *p)
{
    return (unsigned)p[0] << 8 | p[1];
}

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)be16(p) << 16 | be16(p + 2);
}

void dcp_identify_all(uint8_t f[DCP_REQUEST_SIZE], const uint8_t mac[6], uint32_t xid)
{
    static const uint8_t multicast[] = {0x01, 0x0e, 0xcf, 0, 0, 0};
    static const uint8_t header[] = {
        0x88, 0x92, 0xfe, 0xfe, 5, 0, 0, 0, 0, 0, 0, 128, 0, 4, 0xff, 0xff, 0, 0
    };
    memset(f, 0, DCP_REQUEST_SIZE);
    memcpy(f, multicast, sizeof(multicast));
    memcpy(f + 6, mac, 6);
    memcpy(f + 12, header, sizeof(header));
    for (unsigned i = 0; i < 4; ++i)
        f[18 + i] = xid >> (24 - 8 * i);
}

static size_t response_offset(const uint8_t *f, size_t n, const uint8_t mac[6], uint32_t xid)
{
    static const uint8_t zero_mac[6];
    if (!f || n < 14 || memcmp(f, mac, 6) || (f[6] & 1) ||
        !memcmp(f + 6, zero_mac, 6) || !memcmp(f + 6, mac, 6)) return 0;
    size_t offset = 12;
    for (unsigned i = 0; i < 2 && (be16(f + offset) == 0x8100 ||
         be16(f + offset) == 0x88a8); ++i) {
        offset += 4;
        if (n < offset + 2) return 0;
    }
    if (n < offset + 10 || be16(f + offset) != 0x8892 ||
        be16(f + offset + 2) != 0xfeff || f[offset + 4] != 5 ||
        f[offset + 5] != 1 || be32(f + offset + 6) != xid) return 0;
    return offset + 2;
}

bool dcp_matches(const uint8_t *frame, size_t length, const uint8_t mac[6], uint32_t xid)
{
    return response_offset(frame, length, mac, xid) != 0;
}

static bool copy_text(char *out, size_t capacity, const uint8_t *data, size_t length)
{
    bool limited = length >= capacity;
    if (limited) length = capacity - 1;
    for (size_t i = 0; i < length; ++i) {
        uint8_t c = data[i];
        if (c < 32 || c > 126) { c = '?'; limited = true; }
        out[i] = (char)c;
    }
    out[length] = 0;
    return limited;
}

static bool read_block(dcp_device_t *d, unsigned option, unsigned sub, const uint8_t *p, size_t n)
{
    if (option == 1 && (sub == 2 || sub == 3)) {
        if (n < 14 || (sub == 2 ? n != 14 : (n - 14) % 4 != 0)) return false;
        d->fields |= DCP_IP;
        d->ip_info = be16(p);
        memcpy(d->ip, p + 2, 4);
        memcpy(d->mask, p + 6, 4);
        memcpy(d->gateway, p + 10, 4);
    } else if (option == 2 && sub >= 1 && sub <= 4) {
        if (n < 2) return false;
        if (sub == 1) {
            d->fields |= DCP_VENDOR;
            d->text_limited |= copy_text(d->vendor, sizeof(d->vendor), p + 2, n - 2);
        } else if (sub == 2) {
            d->fields |= DCP_NAME;
            d->text_limited |= copy_text(d->name, sizeof(d->name), p + 2, n - 2);
        } else if (sub == 3) {
            if (n != 6) return false;
            d->fields |= DCP_ID;
            d->vendor_id = be16(p + 2);
            d->device_id = be16(p + 4);
        } else {
            if (n != 4) return false;
            d->fields |= DCP_ROLE;
            d->role = p[2];
        }
    }
    return true; // Skip structurally valid unknown blocks.
}

dcp_parse_t dcp_parse(const uint8_t *f, size_t n, const uint8_t mac[6], uint32_t xid,
                      dcp_device_t *device)
{
    size_t off = response_offset(f, n, mac, xid);
    if (!off) return DCP_IGNORE;
    if (n > DCP_FRAME_LIMIT || n - off < 12) return DCP_MALFORMED;
    size_t remaining = be16(f + off + 10);
    off += 12;
    if (remaining > n - off) return DCP_MALFORMED;
    dcp_device_t parsed = {0};
    memcpy(parsed.mac, f + 6, 6);
    while (remaining) {
        if (remaining < 4) return DCP_MALFORMED;
        size_t size = be16(f + off + 2);
        size_t block = 4 + size + (size & 1u);
        if (block > remaining || !read_block(&parsed, f[off], f[off + 1], f + off + 4, size))
            return DCP_MALFORMED;
        off += block;
        remaining -= block;
    }
    *device = parsed;
    return DCP_PARSED;
}

bool dcp_active(dcp_state_t state)
{
    return state == DCP_STARTING || state == DCP_SCANNING;
}

const char *dcp_state_name(dcp_state_t state)
{
    static const char *const names[] = {
        "Unavailable", "Ready", "Starting", "Discovering", "Finished",
        "Cancelled", "Link lost", "Scan failed", "Network changed"
    };
    return names[state];
}

bool dcp_partial(const dcp_summary_t *s)
{
    return s->started_ms && (!s->valid || s->state != DCP_COMPLETE ||
           s->overflow || s->malformed || s->queue_drops || s->text_limited);
}

const char *dcp_ip_status_name(uint16_t status)
{
    switch (status & ~0x80u) {
    case 0: return "IP not set";
    case 1: return "IP set";
    case 2: return "IP set by DHCP";
    default: return "Unknown IP status";
    }
}

void dcp_begin(dcp_inventory_t *inventory, uint32_t xid, uint64_t now)
{
    inventory->summary = (dcp_summary_t){
        .state = DCP_STARTING, .valid = true, .xid = xid, .started_ms = now
    };
}

void dcp_finish(dcp_summary_t *s, dcp_state_t state, uint64_t now)
{
    if (!dcp_active(s->state)) return;
    s->state = state;
    s->finished_ms = now;
}

void dcp_link_down(dcp_summary_t *s, uint64_t now)
{
    s->valid = false;
    dcp_finish(s, DCP_LINK_LOST, now);
}

void dcp_observe(dcp_inventory_t *inventory, const dcp_device_t *device)
{
    dcp_summary_t *s = &inventory->summary;
    dcp_device_t *devices = inventory->devices;
    unsigned index = 0;
    while (index < s->count && memcmp(devices[index].mac, device->mac, 6)) ++index;
    if (index == DCP_CAPACITY) { ++s->overflow; return; }
    if (index == s->count) ++s->count;
    devices[index] = *device;
    uint32_t bit = UINT32_C(1) << index;
    uint32_t names = 0, ips = 0;

    static const uint8_t zero_ip[4];
    for (unsigned i = 0; i < s->count; ++i) {
        if (i == index) continue;
        uint32_t peer = UINT32_C(1) << i;
        inventory->name_matches[i] &= ~bit;
        inventory->ip_matches[i] &= ~bit;
        if (device->name[0] && !strcmp(device->name, devices[i].name)) {
            inventory->name_matches[i] |= bit;
            names |= peer;
        }
        if ((device->fields & DCP_IP) && (devices[i].fields & DCP_IP) &&
            (device->ip_info & ~0x80u) && (devices[i].ip_info & ~0x80u) &&
            memcmp(device->ip, zero_ip, 4) && !memcmp(device->ip, devices[i].ip, 4)) {
            inventory->ip_matches[i] |= bit;
            ips |= peer;
        }
    }
    inventory->name_matches[index] = names;
    inventory->ip_matches[index] = ips;
    s->duplicate_name = s->duplicate_ip = s->reported_conflict = s->text_limited = false;
    for (unsigned i = 0; i < s->count; ++i) {
        devices[i].duplicate_name = inventory->name_matches[i] != 0;
        devices[i].duplicate_ip = inventory->ip_matches[i] != 0;
        s->duplicate_name |= devices[i].duplicate_name;
        s->duplicate_ip |= devices[i].duplicate_ip;
        s->reported_conflict |= (devices[i].fields & DCP_IP) && (devices[i].ip_info & 0x80);
        s->text_limited |= devices[i].text_limited;
    }
}
