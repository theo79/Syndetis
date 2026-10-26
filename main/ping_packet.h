#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PING_PACKET_BYTES 24
void ping_packet_make(uint8_t out[PING_PACKET_BYTES], uint16_t id, uint16_t sequence, uint64_t cookie);
bool ping_packet_match(const uint8_t *ip, size_t length, uint32_t source, uint32_t destination,
                       uint16_t id, uint16_t sequence, uint64_t cookie, bool reply);
bool ping_frame_match(const uint8_t *frame, size_t length, uint32_t source, uint32_t destination,
                      uint16_t id, uint16_t sequence, uint64_t cookie, bool reply);
