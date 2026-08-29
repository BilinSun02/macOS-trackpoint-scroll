#undef CGEventGetLocation
#undef CGEventPost

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#define TPSC_MAX_ACTIVE_DISPLAYS 32u
#define TPSC_DOUBLE_CLICK_FALLBACK_SECONDS 0.5
#define TPSC_DOUBLE_CLICK_SLOP_POINTS 4.0

struct click_tracker {
    bool down;
    bool dragged;
    bool have_last_up;
    CFAbsoluteTime last_up_time;
    CGPoint down_position;
    CGPoint last_click_position;
    int64_t click_state;
    int64_t event_number;
};

static bool g_have_posted_pointer_position;
static CGPoint g_posted_pointer_position;
static CFAbsoluteTime g_posted_pointer_time;

static struct click_tracker g_left_click;
static struct click_tracker g_right_click;
static struct click_tracker g_middle_click;
static int64_t g_next_mouse_event_number = 1;
static CFTimeInterval g_double_click_interval = -1.0;

static bool
is_pointer_motion(CGEventType type)
{
    return type == kCGEventMouseMoved ||
           type == kCGEventLeftMouseDragged ||
           type == kCGEventRightMouseDragged ||
           type == kCGEventOtherMouseDragged;
}

static bool
is_button_down(CGEventType type)
{
    return type == kCGEventLeftMouseDown ||
           type == kCGEventRightMouseDown ||
           type == kCGEventOtherMouseDown;
}

static bool
is_button_up(CGEventType type)
{
    return type == kCGEventLeftMouseUp ||
           type == kCGEventRightMouseUp ||
           type == kCGEventOtherMouseUp;
}

static struct click_tracker *
click_tracker_for_button(int64_t button)
{
    if (button == (int64_t)kCGMouseButtonLeft)
        return &g_left_click;
    if (button == (int64_t)kCGMouseButtonRight)
        return &g_right_click;
    if (button == (int64_t)kCGMouseButtonCenter)
        return &g_middle_click;
    return NULL;
}

static struct click_tracker *
click_tracker_for_drag(CGEventType type)
{
    if (type == kCGEventLeftMouseDragged)
        return &g_left_click;
    if (type == kCGEventRightMouseDragged)
        return &g_right_click;
    if (type == kCGEventOtherMouseDragged)
        return &g_middle_click;
    return NULL;
}

static CFTimeInterval
double_click_interval(void)
{
    CFPropertyListRef value;
    double seconds;

    if (g_double_click_interval >= 0.0)
        return g_double_click_interval;

    g_double_click_interval = TPSC_DOUBLE_CLICK_FALLBACK_SECONDS;
    value = CFPreferencesCopyValue(CFSTR("com.apple.mouse.doubleClickThreshold"),
                                   kCFPreferencesAnyApplication,
                                   kCFPreferencesCurrentUser,
                                   kCFPreferencesAnyHost);
    if (!value)
        return g_double_click_interval;

    if (CFGetTypeID(value) == CFNumberGetTypeID() &&
        CFNumberGetValue((CFNumberRef)value, kCFNumberDoubleType, &seconds) &&
        isfinite(seconds) && seconds > 0.0 && seconds <= 10.0)
        g_double_click_interval = seconds;

    CFRelease(value);
    return g_double_click_interval;
}

static bool
positions_within_click_slop(CGPoint a, CGPoint b)
{
    double dx = (double)a.x - (double)b.x;
    double dy = (double)a.y - (double)b.y;
    double slop = TPSC_DOUBLE_CLICK_SLOP_POINTS;

    return dx * dx + dy * dy <= slop * slop;
}

static int64_t
next_mouse_event_number(void)
{
    int64_t number = g_next_mouse_event_number++;

    if (g_next_mouse_event_number <= 0)
        g_next_mouse_event_number = 1;
    return number;
}

static void
annotate_button_event(CGEventType type, CGEventRef event)
{
    int64_t button;
    struct click_tracker *tracker;
    CGPoint position;
    CFAbsoluteTime now;

    if (!is_button_down(type) && !is_button_up(type))
        return;

    button = CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber);
    tracker = click_tracker_for_button(button);
    if (!tracker)
        return;

    position = CGEventGetLocation(event);
    now = CFAbsoluteTimeGetCurrent();

    if (is_button_down(type)) {
        bool continues_sequence =
            !tracker->down && tracker->have_last_up &&
            now >= tracker->last_up_time &&
            now - tracker->last_up_time <= double_click_interval() &&
            positions_within_click_slop(position, tracker->last_click_position);

        if (!tracker->down) {
            if (continues_sequence && tracker->click_state < INT64_MAX)
                tracker->click_state++;
            else
                tracker->click_state = 1;

            tracker->down = true;
            tracker->dragged = false;
            tracker->down_position = position;
            tracker->last_click_position = position;
            tracker->event_number = next_mouse_event_number();
        }
    } else {
        if (!tracker->down) {
            tracker->click_state = 1;
            tracker->event_number = next_mouse_event_number();
            tracker->have_last_up = false;
        } else {
            if (tracker->dragged ||
                !positions_within_click_slop(position, tracker->down_position)) {
                tracker->have_last_up = false;
            } else {
                tracker->last_up_time = now;
                tracker->have_last_up = true;
            }
            tracker->down = false;
        }
    }

    CGEventSetIntegerValueField(event, kCGMouseEventClickState,
                                tracker->click_state > 0 ? tracker->click_state : 1);
    CGEventSetIntegerValueField(event, kCGMouseEventNumber,
                                tracker->event_number);
}

static void
annotate_drag_event(CGEventType type, CGEventRef event, CGPoint position)
{
    struct click_tracker *tracker = click_tracker_for_drag(type);

    if (!tracker || !tracker->down)
        return;

    if (!positions_within_click_slop(position, tracker->down_position))
        tracker->dragged = true;

    CGEventSetIntegerValueField(event, kCGMouseEventClickState,
                                tracker->click_state > 0 ? tracker->click_state : 1);
    CGEventSetIntegerValueField(event, kCGMouseEventNumber,
                                tracker->event_number);
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
    CGEventType type = CGEventGetType(event);

    annotate_button_event(type, event);

    if (is_pointer_motion(type)) {
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
        annotate_drag_event(type, event, position);
        g_posted_pointer_position = position;
        g_posted_pointer_time = CFAbsoluteTimeGetCurrent();
        g_have_posted_pointer_position = true;
    }

    CGEventPost(tap, event);
}
