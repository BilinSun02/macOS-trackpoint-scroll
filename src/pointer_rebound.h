#ifndef MACOS_TRACKPOINT_SCROLL_POINTER_REBOUND_H
#define MACOS_TRACKPOINT_SCROLL_POINTER_REBOUND_H

#include <ApplicationServices/ApplicationServices.h>
#include <stdbool.h>

/* Enable/disable the optional terminal TrackPoint rebound correction. */
int tpsc_pointer_rebound_set_enabled(bool enabled);

/* Observe an already-transformed synthetic TrackPoint pointer event. */
void tpsc_pointer_rebound_observe(CGEventType type, CGEventRef event);

/* Hard gesture/input boundary: discard any pending retrospective correction. */
void tpsc_pointer_rebound_reset(void);

#endif
