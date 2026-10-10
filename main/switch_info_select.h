#pragma once
#include "switch_info_parse.h"

/* Internal observations only. Consumers still receive one common snapshot. */
typedef struct { switch_neighbor_t lldp, cdp; } switch_info_observations_t;
unsigned switch_info_expire(switch_info_observations_t *state, bool up, uint32_t generation, int64_t now);
const switch_neighbor_t *switch_info_selected(const switch_info_observations_t *state);
/* Input metadata is stamped by RX/worker, not by publication time. */
bool switch_info_observe(switch_info_observations_t *state, const switch_neighbor_t *neighbor, int64_t now);
bool switch_info_capture_current(bool captured_up, uint32_t captured_generation, uint32_t captured_epoch,
                                 bool up, uint32_t generation, uint32_t epoch);
