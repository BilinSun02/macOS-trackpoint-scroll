#ifndef MACOS_TRACKPOINT_SCROLL_EDGE_PRESSURE_PROTOCOL_H
#define MACOS_TRACKPOINT_SCROLL_EDGE_PRESSURE_PROTOCOL_H

#include <stdint.h>

#define TPSC_EDGE_PRESSURE_SOCKET_PREFIX \
    "/var/run/io.github.bilinsun02.macos-trackpoint-scroll.edge-pressure."

struct tpsc_edge_pressure_message {
    int64_t dx;
    int64_t dy;
    int64_t vertical_wheel;
    int64_t horizontal_wheel;
    uint32_t buttons;
};

#endif
