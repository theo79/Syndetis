#include "ui_switch_info.h"

/* Each OLED row holds 21 printable characters plus its terminator. Network
 * fields are scanned only within their fixed model capacity, even if damaged.
 * No network value is used as a format string. */
static size_t text_length(const char *text, size_t capacity)
{
    size_t length = 0;
    while (length < capacity && text[length]) ++length;
    return length;
}

static void text_row(char row[22], const char *text, size_t capacity, size_t offset)
{
    size_t length = text_length(text, capacity);
    size_t count = offset < length ? length - offset : 0;
    if (count > 21) count = 21;
    for (size_t i = 0; i < count; ++i) {
        unsigned char c = (unsigned char)text[offset + i];
        row[i] = c >= 32 && c <= 126 ? (char)c : '.';
    }
    row[count] = 0;
    if (offset + count < length && count == 21) {
        row[18] = row[19] = row[20] = '.';
    }
}

static void label(char row[22], const char *text)
{
    text_row(row, text, 22, 0);
}

static void field(char row[22], const char *text, size_t capacity)
{
    if (text[0]) text_row(row, text, capacity, 0);
    else label(row, "N/A");
}

static void append_number(char row[22], unsigned value, unsigned base, unsigned width)
{
    char digits[10];
    size_t count = 0;
    do {
        unsigned digit = value % base;
        digits[count++] = digit < 10 ? '0' + digit : 'A' + digit - 10;
        value /= base;
    } while (value && count < sizeof(digits));
    while (count < width && count < sizeof(digits)) digits[count++] = '0';
    size_t used = text_length(row, 21);
    while (count && used < 21) row[used++] = digits[--count];
    row[used] = 0;
}

static void append_text(char row[22], const char *text)
{
    size_t used = text_length(row, 21);
    size_t count = text_length(text, 22);
    for (size_t i = 0; i < count && used < 21; ++i) row[used++] = text[i];
    row[used] = 0;
}

static const char *protocol_name(switch_protocol_t protocol)
{
    switch (protocol) {
    case SWITCH_PROTOCOL_LLDP: return "LLDP";
    case SWITCH_PROTOCOL_CDP: return "CDP";
    case SWITCH_PROTOCOL_NONE: return "NONE";
    default: return "UNKNOWN";
    }
}

static void capability_labels(char row[22], uint16_t mask)
{
    /* Full masks remain visible even when the abbreviation list is shortened. */
    const char *names[] = {"Other", "Repeater", "Bridge", "AP", "Router",
                          "Phone", "DOCSIS", "Station", "C-VLAN", "S-VLAN", "TPMR"};
    char text[96] = {0};
    size_t used = 0;
    for (unsigned bit = 0; bit < 11; ++bit) {
        if (!(mask & (1U << bit))) continue;
        if (used) text[used++] = ' ';
        for (size_t i = 0; names[bit][i] && used < sizeof(text) - 1; ++i)
            text[used++] = names[bit][i];
    }
    if (mask & 0xF800) {
        if (used) text[used++] = ' ';
        const char *unknown = "Unknown";
        for (size_t i = 0; unknown[i] && used < sizeof(text) - 1; ++i)
            text[used++] = unknown[i];
    }
    if (!mask) label(row, "None");
    else text_row(row, text, sizeof(text), 0);
}

unsigned ui_switch_info_page(unsigned page, int direction)
{
    page %= UI_SWITCH_INFO_PAGES;
    if (direction < 0) return (page + UI_SWITCH_INFO_PAGES - 1) % UI_SWITCH_INFO_PAGES;
    if (direction > 0) return (page + 1) % UI_SWITCH_INFO_PAGES;
    return page;
}

void ui_switch_info_render(char rows[8][22], unsigned page, bool available,
                           int setup_error, const switch_neighbor_t *neighbor)
{
    for (unsigned r = 0; r < 8; ++r)
        for (unsigned c = 0; c < 22; ++c) rows[r][c] = 0;
    label(rows[0], "SWITCH INFO");
    label(rows[7], "OK: BACK");
    if (!available) {
        label(rows[2], "Unavailable");
        label(rows[4], "Listener setup error");
        label(rows[5], "Code: 0x");
        append_number(rows[5], (unsigned)setup_error, 16, 0);
        return;
    }
    if (!neighbor->valid) {
        label(rows[2], "No neighbor found");
        label(rows[4], "Waiting for LLDP/CDP");
        label(rows[6], "Listener active");
        return;
    }
    page %= UI_SWITCH_INFO_PAGES;
    const char *titles[] = {"SWITCH INFO ", "IDENTITY ", "NETWORK ", "DESCRIPTION ", "CAPABILITIES "};
    label(rows[0], titles[page]);
    append_number(rows[0], page + 1, 10, 0);
    append_text(rows[0], "/5");
    label(rows[7], "UP/DOWN PAGE OK BACK");
    switch (page) {
    case 0:
        label(rows[1], "Proto: "); append_text(rows[1], protocol_name(neighbor->protocol));
        label(rows[2], "Name:"); field(rows[3], neighbor->system_name, sizeof(neighbor->system_name));
        label(rows[4], "Port:"); field(rows[5], neighbor->port_id, sizeof(neighbor->port_id));
        label(rows[6], "TTL: "); append_number(rows[6], neighbor->ttl, 10, 0); append_text(rows[6], "s");
        break;
    case 1:
        label(rows[2], neighbor->protocol == SWITCH_PROTOCOL_CDP ? "Device:" : "Chassis:");
        field(rows[3], neighbor->chassis_id, sizeof(neighbor->chassis_id));
        label(rows[4], "Port:"); field(rows[5], neighbor->port_id, sizeof(neighbor->port_id));
        /* Source MAC is the final identity row, in conventional byte order. */
        label(rows[6], "MAC:");
        for (unsigned i = 0; i < 6; ++i) {
            if (i) append_text(rows[6], ":");
            append_number(rows[6], neighbor->source_mac[i], 16, 2);
        }
        break;
    case 2:
        label(rows[2], "Mgmt IPv4:");
        if (neighbor->management_ipv4_present) {
            for (unsigned i = 0; i < 4; ++i) {
                if (i) append_text(rows[3], ".");
                append_number(rows[3], neighbor->management_ipv4[i], 10, 0);
            }
        } else label(rows[3], "N/A");
        label(rows[5], neighbor->protocol == SWITCH_PROTOCOL_CDP ? "Native VLAN: " : "PVID: ");
        if (neighbor->pvid_present) append_number(rows[5], neighbor->pvid, 10, 0);
        else append_text(rows[5], "N/A");
        if (neighbor->duplex_present) label(rows[6], neighbor->full_duplex ? "Peer duplex: FULL" : "Peer duplex: HALF");
        break;
    case 3: {
        label(rows[1], "System description:");
        field(rows[2], neighbor->system_description, sizeof(neighbor->system_description));
        /* The first row continues onto the second, so only the final row gets ellipsis. */
        if (text_length(neighbor->system_description, sizeof(neighbor->system_description)) > 21) {
            for (unsigned i = 18; i < 21; ++i) {
                unsigned char c = (unsigned char)neighbor->system_description[i];
                rows[2][i] = c >= 32 && c <= 126 ? (char)c : '.';
            }
            text_row(rows[3], neighbor->system_description, sizeof(neighbor->system_description), 21);
        }
        const char *description = neighbor->protocol == SWITCH_PROTOCOL_CDP ? neighbor->platform : neighbor->port_description;
        size_t description_size = neighbor->protocol == SWITCH_PROTOCOL_CDP ? sizeof(neighbor->platform) : sizeof(neighbor->port_description);
        label(rows[4], neighbor->protocol == SWITCH_PROTOCOL_CDP ? "Platform:" : "Port description:");
        field(rows[5], description, description_size);
        if (text_length(description, description_size) > 21) {
            for (unsigned i = 18; i < 21; ++i) {
                unsigned char c = (unsigned char)description[i];
                rows[5][i] = c >= 32 && c <= 126 ? (char)c : '.';
            }
            text_row(rows[6], description, description_size, 21);
        }
        break;
    }
    case 4:
        if (neighbor->cdp_capabilities_present) {
            label(rows[2], "CDP raw: 0x"); append_number(rows[2], neighbor->cdp_capabilities, 16, 8);
            label(rows[4], "Enabled: N/A");
        } else if (neighbor->capabilities_present) {
            label(rows[2], "Supported: 0x"); append_number(rows[2], neighbor->supported_capabilities, 16, 4);
            capability_labels(rows[3], neighbor->supported_capabilities);
            label(rows[4], "Enabled: 0x"); append_number(rows[4], neighbor->enabled_capabilities, 16, 4);
            capability_labels(rows[5], neighbor->enabled_capabilities);
        } else {
            label(rows[2], "Supported: N/A");
            label(rows[4], "Enabled: N/A");
        }
        break;
    }
}
