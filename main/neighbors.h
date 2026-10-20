#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEIGHBOR_CAPACITY 4
#define NEIGHBOR_FRAME_MAX 1518
#define NEIGHBOR_OBSERVE_MS 30000
enum { PROTOCOL_LLDP = 1, PROTOCOL_CDP = 2 };
enum { FIELD_NAME = 1, FIELD_CHASSIS = 2, FIELD_PORT = 4, FIELD_DESCRIPTION = 8,
       FIELD_MANAGEMENT = 16, FIELD_VLAN = 32 };
typedef enum { PARSE_IGNORE, PARSE_VALID, PARSE_INVALID, PARSE_LIMIT } parse_result_t;
typedef enum { DISC_LINK_DOWN, DISC_WAITING, DISC_NONE, DISC_CURRENT, DISC_EXPIRED } discovery_state_t;

typedef struct {
    bool used, sanitized;
    uint8_t protocol, source[6], fields, management_kind; // 1=management TLV, 2=CDP address TLV
    uint8_t chassis_subtype, port_subtype;
    uint16_t chassis_length, port_length, ttl, vlan;
    uint8_t chassis_key[255], port_key[255]; // Full identity, never shortened for matching.
    char name[256], chassis[512], port[512], description[256], management[48];
    uint64_t received_ms;
} neighbor_t;

typedef struct {
    bool link_up, ever_received;
    uint32_t session, capacity_drops, invalid_frames, limited_frames, queue_drops;
    uint64_t link_since_ms;
    neighbor_t records[NEIGHBOR_CAPACITY];
} neighbor_table_t;

// Portable, allocation-free parser and lifecycle used by firmware and host checks.
bool neighbor_destination(const uint8_t *frame, size_t length);
parse_result_t neighbor_parse(const uint8_t *frame, size_t length, neighbor_t *result);
void neighbors_link(neighbor_table_t *table, bool up, uint64_t now_ms);
bool neighbor_expired(const neighbor_t *record, uint64_t now_ms);
bool neighbors_accept(neighbor_table_t *table, const neighbor_t *record,
                      uint32_t session, uint64_t received_ms, uint64_t now_ms);
discovery_state_t neighbors_state(const neighbor_table_t *table, uint64_t now_ms);
const char *neighbors_state_name(discovery_state_t state);
