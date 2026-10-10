#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SWITCH_INFO_FRAME_SIZE 1600
#define SWITCH_INFO_MAX_TLVS 64
#define SWITCH_INFO_ID_SIZE 96
#define SWITCH_INFO_TEXT_SIZE 128
#define SWITCH_INFO_DESCRIPTION_SIZE 256
#define SWITCH_INFO_RAW_ID_SIZE 256

typedef enum { SWITCH_PROTOCOL_NONE, SWITCH_PROTOCOL_LLDP, SWITCH_PROTOCOL_CDP } switch_protocol_t;
/* LLDP raw identifiers include subtype; CDP retains the complete Device/Port
 * ID bytes. Preserve equality when printable text is truncated or sanitized.
 * No dynamic storage and no invented optional data. */
typedef struct {
    bool valid;
    switch_protocol_t protocol;
    int64_t receive_timestamp_us, expiration_us;
    uint32_t link_generation;
    uint16_t ttl;
    uint8_t source_mac[6];
    char chassis_id[SWITCH_INFO_ID_SIZE], device_id[SWITCH_INFO_ID_SIZE];
    char port_id[SWITCH_INFO_ID_SIZE];
    char port_description[SWITCH_INFO_TEXT_SIZE], system_name[SWITCH_INFO_TEXT_SIZE];
    char system_description[SWITCH_INFO_DESCRIPTION_SIZE];
    char platform[SWITCH_INFO_TEXT_SIZE];
    bool duplex_present, full_duplex, cdp_capabilities_present;
    uint8_t cdp_version;
    uint32_t cdp_capabilities; /* Raw CDP bits, never passed off as LLDP masks. */
    bool management_ipv4_present, pvid_present, capabilities_present;
    uint8_t management_ipv4[4];
    uint16_t pvid, supported_capabilities, enabled_capabilities;
    uint16_t chassis_raw_len, port_raw_len;
    uint8_t chassis_raw[SWITCH_INFO_RAW_ID_SIZE], port_raw[SWITCH_INFO_RAW_ID_SIZE];
} switch_neighbor_t;

/* Phase B: nearest-bridge, untagged only. Never mutates frame. */
bool switch_info_is_lldp(const uint8_t *frame, size_t length);
/* On failure out is zeroed. A valid withdrawal returns true with ttl==0. */
bool switch_info_parse_lldp(const uint8_t *frame, size_t length, switch_neighbor_t *out);
/* Untagged 802.3 + Cisco LLC/SNAP only; padding is outside the declared PDU. */
bool switch_info_is_cdp(const uint8_t *frame, size_t length);
bool switch_info_parse_cdp(const uint8_t *frame, size_t length, switch_neighbor_t *out);
bool switch_info_same_neighbor(const switch_neighbor_t *a, const switch_neighbor_t *b);
bool switch_info_neighbor_current(const switch_neighbor_t *n, bool link_up,
                                  uint32_t generation, int64_t now_us);
