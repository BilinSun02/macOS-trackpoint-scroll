#ifndef MACOS_TRACKPOINT_SCROLL_REBOUND_FILTER_H
#define MACOS_TRACKPOINT_SCROLL_REBOUND_FILTER_H

#include <ApplicationServices/ApplicationServices.h>

/* Observe an already-transformed synthetic TrackPoint pointer event. */
void tpsc_rebound_filter_observe(CGEventType type, CGEventRef event);

/* Hard gesture/input boundary: discard any pending retrospective correction. */
void tpsc_rebound_filter_reset(void);

#endif
