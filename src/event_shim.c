#undef CGEventGetLocation
#undef CGEventPost

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdbool.h>
#include <stdint.h>

#include "config.h"

static bool g_have_posted_pointer_position;
static CGPoint g_posted_pointer_position;
static CFAbsoluteTime g_posted_pointer_time;
static bool g_pointer_speed_loaded;
static double g_pointer_speed = 1.0;
static double g_pointer_remainder_x;
static double g_pointer_remainder_y;

static bool
is_pointer_motion(CGEventType type)
{
    return type == kCGEventMouseMoved ||
           type == kCGEventLeftMouseDragged ||
           type == kCGEventRightMouseDragged ||
           type == kCGEventOtherMouseDragged;
}

static void
load_pointer_speed_once(void)
{
    struct macos_trackpoint_config cfg;
    char path[1024];
    const char *selected;

    if (g_pointer_speed_loaded)
        return;
    g_pointer_speed_loaded = true;

    macos_trackpoint_config_defaults(&cfg);
    selected = macos_trackpoint_default_config_path(path, sizeof(path));
    if (selected && macos_trackpoint_config_load(&cfg, selected, false) == 0)
        g_pointer_speed = cfg.pointer_speed;
}

static int64_t
scale_axis(int64_t delta, double *remainder)
{
    double total;
    double integral;

    if (g_pointer_speed == 1.0)
        return delta;

    total = (double)delta * g_pointer_speed + *remainder;
    integral = trunc(total);
    *remainder = total - integral;
    return (int64_t)integral;
}

static void
scale_pointer_event(CGEventRef event)
{
    int64_t raw_dx;
    int64_t raw_dy;
    int64_t out_dx;
    int64_t out_dy;
    CGPoint position;

    load_pointer_speed_once();
    if (g_pointer_speed == 1.0)
        return;

    raw_dx = CGEventGetIntegerValueField(event, kCGMouseEventDeltaX);
    raw_dy = CGEventGetIntegerValueField(event, kCGMouseEventDeltaY);
    out_dx = scale_axis(raw_dx, &g_pointer_remainder_x);
    out_dy = scale_axis(raw_dy, &g_pointer_remainder_y);

    position = CGEventGetLocation(event);
    position.x += (double)(out_dx - raw_dx);
    position.y += (double)(out_dy - raw_dy);

    CGEventSetIntegerValueField(event, kCGMouseEventDeltaX, out_dx);
    CGEventSetIntegerValueField(event, kCGMouseEventDeltaY, out_dy);
    CGEventSetLocation(event, position);
}

CGPoint
tpsc_event_get_location(CGEventRef event)
{
    const CFTimeInterval split_axis_window = 0.050;
    CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();

    if (g_have_posted_pointer_position &&
        now - g_posted_pointer_time <= split_axis_window)
        return g_posted_pointer_position;

    g_have_posted_pointer_position = false;
    return CGEventGetLocation(event);
}

void
tpsc_event_post(CGEventTapLocation tap, CGEventRef event)
{
    if (is_pointer_motion(CGEventGetType(event))) {
        scale_pointer_event(event);
        g_posted_pointer_position = CGEventGetLocation(event);
        g_posted_pointer_time = CFAbsoluteTimeGetCurrent();
        g_have_posted_pointer_position = true;
    }

    CGEventPost(tap, event);
}
