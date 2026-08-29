#ifndef MACOS_TRACKPOINT_SCROLL_EVENT_SHIM_H
#define MACOS_TRACKPOINT_SCROLL_EVENT_SHIM_H

#include <ApplicationServices/ApplicationServices.h>

CGPoint tpsc_event_get_location(CGEventRef event);
void tpsc_event_post(CGEventTapLocation tap, CGEventRef event);

/*
 * The seized-device forwarding path posts absolute Quartz mouse positions.
 * The adapter reports X and Y as separate HID element callbacks, and Quartz
 * posting is asynchronous enough that immediately querying the cursor for the
 * second axis can still return the position from before the first axis post.
 * Route those two calls through a tiny cache so split-axis reports compose
 * instead of overwriting each other.
 */
#define CGEventGetLocation tpsc_event_get_location
#define CGEventPost tpsc_event_post

#endif
