#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define OUI_VENDOR_LEN 32
/* Clears valid output on failure. Truncates and terminates on success, including
 * a one-byte output. Null MAC/output or zero capacity returns false.
 * Only globally administered unicast addresses may match the bounded table. */
bool oui_lookup_vendor(const uint8_t mac[6], char *out, size_t out_len);
