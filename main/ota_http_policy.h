#pragma once
#include <stdbool.h>
#include <stdint.h>
#define OTA_HTTP_PORT 8080u
// Exact current socket IPv4:8080 and same-origin HTTP; no aliases or CORS.
bool ota_http_authority(uint32_t local, const char *host, const char *origin, bool mutation);
