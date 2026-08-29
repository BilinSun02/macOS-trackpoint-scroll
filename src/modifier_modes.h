#ifndef MACOS_TRACKPOINT_SCROLL_MODIFIER_MODES_H
#define MACOS_TRACKPOINT_SCROLL_MODIFIER_MODES_H

#include "trackpoint_scroll/engine.h"

int tpsc_modifier_engine_begin(struct tpsc_engine *engine, uint64_t time_us);
int tpsc_modifier_engine_end(struct tpsc_engine *engine, uint64_t time_us);
int tpsc_modifier_engine_feed(struct tpsc_engine *engine,
                              uint64_t time_us,
                              struct tpsc_vec delta);
int tpsc_modifier_engine_tick(struct tpsc_engine *engine,
                              uint64_t time_us,
                              struct tpsc_vec *output);

#endif
