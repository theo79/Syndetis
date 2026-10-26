#pragma once
#include <stdlib.h>
#include "discovery.h"
#include "dhcp_test.h"

static inline void ethernet_dispatch(uint8_t *buffer, uint32_t length)
{
    discovery_receive(buffer, length); // Borrow/copy; never consumes the original.
    if (!dhcp_test_receive(buffer, length)) free(buffer);
}
