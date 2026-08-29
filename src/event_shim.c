#undef CGEventGetLocation
#undef CGEventPost

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#define TPSC_MAX_ACTIVE_DISPLAYS 32u

static bool g_have_posted_pointer_position;
static CGPoint g_posted_pointer_position;
static CFAbsoluteTime g_posted_pointer_time;

static bool
is_pointer_motion(CGEventType type)
{
    return type == kCGEventMouseMoved ||
           type == kCGEventLeftMouseDragged ||
           type == kCGEventRightMouseDragged ||
           type == kCGEventOtherMouseDragged;
}

static CGFloat
clamp_display_coordinate(CGFloat value, CGFloat minimum, CGFloat maximum)
{
    CGFloat last_inside;

    if (maximum <= minimum)
        return minimum;

    /* CGRect's maximum edge is outside the display's addressable area. */
    last_inside = nextafter(maximum, minimum);
    if (value < minimum)
        return minimum;
    if (value > last_inside)
        return last_inside;
    return value;
}

static CGPoint
project_to_active_displays(CGPoint position)
{
    CGDirectDisplayID displays[TPSC_MAX_ACTIVE_DISPLAYS];
    uint32_t count = 0;
    CGPoint best = position;
    double best_distance2 = INFINITY;
    uint32_t i;

    if (CGGetActiveDisplayList(TPSC_MAX_ACTIVE_DISPLAYS,
                               displays, &count) != kCGErrorSuccess ||
        count == 0)
        return position;

    for (i = 0; i < count; i++) {
        CGRect bounds = CGDisplayBounds(displays[i]);
        CGFloat min_x = CGRectGetMinX(bounds);
        CGFloat min_y = CGRectGetMinY(bounds);
        CGFloat max_x = CGRectGetMaxX(bounds);
        CGFloat max_y = CGRectGetMaxY(bounds);
        CGPoint candidate;
        double dx;
        double dy;
        double distance2;

        candidate.x = clamp_display_coordinate(position.x, min_x, max_x);
        candidate.y = clamp_display_coordinate(position.y, min_y, max_y);

        if (candidate.x == position.x && candidate.y == position.y)
            return position;

        dx = (double)(candidate.x - position.x);
        dy = (double)(candidate.y - position.y);
        distance2 = dx * dx + dy * dy;
        if (distance2 < best_distance2) {
            best_distance2 = distance2;
            best = candidate;
        }
    }

    return best;
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
        /*
         * Quartz clamps the visible cursor to active display geometry, but the
         * synthetic event itself may carry an out-of-bounds absolute position.
         * Never cache that impossible position: otherwise repeated outward
         * motion at a screen edge accumulates invisible overshoot that must be
         * cancelled before inward motion becomes visible.
         *
         * Project first, then both post and cache the same realizable position.
         * This preserves the split-axis composition fix without creating a
         * hidden off-screen cursor reservoir.
         */
        CGPoint position = project_to_active_displays(CGEventGetLocation(event));

        CGEventSetLocation(event, position);
        g_posted_pointer_position = position;
        g_posted_pointer_time = CFAbsoluteTimeGetCurrent();
        g_have_posted_pointer_position = true;
    }

    CGEventPost(tap, event);
}
