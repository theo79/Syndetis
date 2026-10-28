#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OTA_PREFIX_SIZE 288u
#define OTA_AUTHORIZE_MS 180000u
#define OTA_UPLOAD_MS 600000u
#define OTA_STALL_MS 10000u

typedef enum { OTA_IDLE, OTA_AUTHORIZED, OTA_RECEIVING, OTA_VALIDATING,
               OTA_READY, OTA_FAILED, OTA_CANCELLED } ota_state_t;
// Caller serializes engine operations. Callbacks never run in a critical section.
// begin/write/finish target an inactive app only; finish validates the complete
// raw ESP image before activate may select it. No signature/authenticity claim.
typedef struct {
    bool (*begin)(void *, size_t);
    bool (*write)(void *, const uint8_t *, size_t);
    bool (*finish)(void *);
    bool (*activate)(void *);
    void (*abort)(void *);
    void *ctx;
} ota_io_t;
typedef struct {
    ota_state_t state;
    const char *message;
    ota_io_t io;
    size_t capacity, image_size, received, written, prefix_used;
    uint64_t deadline, last_rx;
    bool writer_open;
    char current_version[32];
    uint8_t prefix[OTA_PREFIX_SIZE];
} ota_engine_t;

void ota_engine_init(ota_engine_t *, const ota_io_t *, const char *version);
bool ota_engine_authorize(ota_engine_t *, size_t capacity, uint64_t now);
bool ota_engine_session_valid(ota_engine_t *, uint64_t now);
bool ota_engine_begin(ota_engine_t *, size_t upload_size, uint64_t now);
bool ota_engine_feed(ota_engine_t *, const uint8_t *, size_t, uint64_t now);
bool ota_engine_finish(ota_engine_t *, uint64_t now);
void ota_engine_tick(ota_engine_t *, uint64_t now);
void ota_engine_cancel(ota_engine_t *, const char *reason);
const char *ota_state_name(ota_state_t);
bool ota_version_newer(const char *candidate, const char *current);
