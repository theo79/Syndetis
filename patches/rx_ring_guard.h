#pragma once
#include <stdbool.h>
#ifdef __cplusplus
static inline constexpr bool rx_ring_step(unsigned *walked, unsigned limit)
#else
static inline bool rx_ring_step(unsigned *walked, unsigned limit)
#endif
{
    if (*walked >= limit) return false;
    ++*walked;
    return true;
}
