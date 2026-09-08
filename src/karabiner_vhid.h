#ifndef MACOS_TRACKPOINT_SCROLL_KARABINER_VHID_H
#define MACOS_TRACKPOINT_SCROLL_KARABINER_VHID_H
#include <stdbool.h>
#include <stdint.h>
int tpsc_vhid_initialize(void);
void tpsc_vhid_shutdown(void);
bool tpsc_vhid_is_enabled(void);
bool tpsc_vhid_keepalive(void);
bool tpsc_vhid_post_relative(int64_t dx, int64_t dy);
#endif
