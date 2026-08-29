#ifndef MACOS_TRACKPOINT_SCROLL_MODIFIER_INTERPOSE_H
#define MACOS_TRACKPOINT_SCROLL_MODIFIER_INTERPOSE_H

#include "modifier_modes.h"

#define tpsc_engine_begin tpsc_modifier_engine_begin
#define tpsc_engine_end   tpsc_modifier_engine_end
#define tpsc_engine_feed  tpsc_modifier_engine_feed
#define tpsc_engine_tick  tpsc_modifier_engine_tick

#endif
