#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
#define DIAG_INLINE static inline constexpr
#else
#define DIAG_INLINE static inline
#endif
typedef struct { uint32_t sequence, active; } diagnostic_reservation_t;
DIAG_INLINE uint32_t diagnostic_reserve(diagnostic_reservation_t *s)
{
    if (s->active) return 0;
    if (++s->sequence == 0) ++s->sequence;
    return s->active=s->sequence;
}
DIAG_INLINE bool diagnostic_unreserve(diagnostic_reservation_t *s, uint32_t token)
{
    if (!token || s->active != token) return false;
    s->active=0;
    return true;
}
DIAG_INLINE bool diagnostic_accept(uint32_t token, uint32_t active,
                                  uint32_t generation, uint32_t current, bool link)
{ return token && token==active && generation==current && link; }
typedef struct { bool link_up, ipv4_ready; uint32_t link_generation, revision; } network_lifecycle_t;
DIAG_INLINE void network_link_event(network_lifecycle_t *n, bool up)
{
    n->link_up=up;
    n->ipv4_ready=false;
    ++n->link_generation; ++n->revision;
}
DIAG_INLINE bool network_ip_event(network_lifecycle_t *n, bool acquired, bool changed)
{
    if (acquired && !n->link_up) return false;
    n->ipv4_ready=acquired;
    if (!acquired) ++n->link_generation;
    if (!acquired || changed) ++n->revision;
    return true;
}
#undef DIAG_INLINE
