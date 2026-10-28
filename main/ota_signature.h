#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mbedtls/pk.h"
// Caller initializes/frees the PK context. Only P-256 public verification keys.
bool ota_signature_load(mbedtls_pk_context *,const uint8_t *pem,size_t length);
bool ota_signature_verify(mbedtls_pk_context *,const uint8_t image_digest[32],const uint8_t trailer[128]);
