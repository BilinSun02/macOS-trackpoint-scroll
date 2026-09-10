#ifndef MACOS_TRACKPOINT_SCROLL_EDGE_PRESSURE_CLIENT_H
#define MACOS_TRACKPOINT_SCROLL_EDGE_PRESSURE_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

void tpsc_edge_pressure_client_set_enabled(bool enabled);
bool tpsc_edge_pressure_post(int64_t dx, int64_t dy);
bool tpsc_edge_pressure_post_state(int64_t dx, int64_t dy, uint32_t buttons);

#endif
