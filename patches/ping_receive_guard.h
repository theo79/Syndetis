#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
static inline constexpr bool ping_receive_continue(bool started, uint32_t elapsed, uint32_t timeout)
#else
static inline bool ping_receive_continue(bool started, uint32_t elapsed, uint32_t timeout)
#endif
{ return started && elapsed < timeout; }
