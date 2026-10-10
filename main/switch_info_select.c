#include "switch_info_select.h"
#include <string.h>

unsigned switch_info_expire(switch_info_observations_t *state, bool up, uint32_t generation, int64_t now)
{
    unsigned expired = 0;
    switch_neighbor_t *records[] = {&state->lldp, &state->cdp};
    for (unsigned i = 0; i < 2; ++i) {
        switch_neighbor_t *n = records[i];
        if (n->valid && !switch_info_neighbor_current(n, up, generation, now)) {
            if (up && n->link_generation == generation && now >= n->expiration_us) ++expired;
            memset(n, 0, sizeof(*n));
        }
    }
    return expired;
}

const switch_neighbor_t *switch_info_selected(const switch_info_observations_t *state)
{
    if (state->lldp.valid) return &state->lldp;
    if (state->cdp.valid) return &state->cdp;
    return NULL; /* Caller expires observations before selecting. */
}

bool switch_info_observe(switch_info_observations_t *state, const switch_neighbor_t *neighbor, int64_t now)
{
    switch_neighbor_t *target;
    if (neighbor->protocol == SWITCH_PROTOCOL_LLDP) target = &state->lldp;
    else if (neighbor->protocol == SWITCH_PROTOCOL_CDP) target = &state->cdp;
    else return false;
    if (!neighbor->ttl) {
        if (!switch_info_same_neighbor(target, neighbor)) return false;
        memset(target, 0, sizeof(*target));
    } else {
        if (!neighbor->valid || now >= neighbor->expiration_us) return false;
        *target = *neighbor;
    }
    return true;
}

bool switch_info_capture_current(bool captured_up, uint32_t captured_generation, uint32_t captured_epoch,
                                 bool up, uint32_t generation, uint32_t epoch)
{
    return captured_up && up && captured_generation == generation && captured_epoch == epoch;
}
