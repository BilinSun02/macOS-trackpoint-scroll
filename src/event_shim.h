#ifndef MACOS_TRACKPOINT_SCROLL_EVENT_SHIM_H
#define MACOS_TRACKPOINT_SCROLL_EVENT_SHIM_H

#include <ApplicationServices/ApplicationServices.h>

CGPoint tpsc_event_get_location(CGEventRef event);
void tpsc_event_post(CGEventTapLocation tap, CGEventRef event);

/*
 * Compatibility mode (seized operation without the virtual-HID helper) posts
 * absolute Quartz mouse positions. The adapter reports X and Y as separate HID
 * callbacks, and Quartz posting is asynchronous enough that the second axis can
 * still query the position from before the first-axis post. Route those calls
 * through a tiny cache so split-axis reports compose instead of overwriting one
 * another. The normal installed helper-backed path does not use this pointer
 * posting shim.
 */
#define CGEventGetLocation tpsc_event_get_location
#define CGEventPost tpsc_event_post

#endif
