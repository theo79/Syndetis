#pragma once
#include "switch_info_parse.h"

#define UI_SWITCH_INFO_PAGES 5
/* Pure presentation helpers. Caller supplies a copied, current snapshot. */
unsigned ui_switch_info_page(unsigned page, int direction);
void ui_switch_info_render(char rows[8][22], unsigned page, bool available,
                           int setup_error, const switch_neighbor_t *neighbor);
