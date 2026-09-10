#include "pointer_rebound.h"

#include <CoreFoundation/CoreFoundation.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "trackpoint_scroll/rebound.h"

#define TPSC_REBOUND_POSITION_SLOP_POINTS 1.0

static bool g_enabled;
static struct tpsc_rebound_filter *g_filter;
static CFRunLoopTimerRef g_timer;

static bool g_have_last_trackpoint_position;
static CGPoint g_last_trackpoint_position;
static bool g_posting_correction;

/* Implemented by event_shim.c so correction follows the same pointer path. */
void tpsc_event_post(CGEventTapLocation tap, CGEventRef event);

static uint64_t
now_us(void)
{
    long double value =
        (long double)CFAbsoluteTimeGetCurrent() * 1000000.0L;

    if (value <= 0.0L)
        return 0;
    if (value >= (long double)UINT64_MAX)
        return UINT64_MAX;
    return (uint64_t)value;
}

static void
cancel_timer(void)
{
    if (!g_timer)
        return;

    CFRunLoopTimerInvalidate(g_timer);
    CFRelease(g_timer);
    g_timer = NULL;
}

static bool
current_cursor_position(CGPoint *position)
{
    CGEventRef event = CGEventCreate(NULL);

    if (!event)
        return false;

    *position = CGEventGetLocation(event);
    CFRelease(event);
    return true;
}

static bool
cursor_still_at_last_trackpoint_position(void)
{
    CGPoint current;
    double dx;
    double dy;
    double slop = TPSC_REBOUND_POSITION_SLOP_POINTS;

    if (!g_have_last_trackpoint_position)
        return false;
    if (!current_cursor_position(&current))
        return false;

    dx = (double)(current.x - g_last_trackpoint_position.x);
    dy = (double)(current.y - g_last_trackpoint_position.y);
    return dx * dx + dy * dy <= slop * slop;
}

static int64_t
correction_field(double value)
{
    if (value >= (double)INT64_MAX)
        return INT64_MAX;
    if (value <= (double)INT64_MIN)
        return INT64_MIN;
    return (int64_t)llround(value);
}

static void
post_correction(struct tpsc_vec correction)
{
    CGEventRef event;
    CGPoint position;
    int64_t dx;
    int64_t dy;

    if (correction.x == 0.0 && correction.y == 0.0)
        return;
    if (!current_cursor_position(&position))
        return;

    dx = correction_field(correction.x);
    dy = correction_field(correction.y);

    position.x += correction.x;
    position.y += correction.y;

    event = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved, position,
                                    kCGMouseButtonLeft);
    if (!event)
        return;

    CGEventSetIntegerValueField(event, kCGMouseEventDeltaX, dx);
    CGEventSetIntegerValueField(event, kCGMouseEventDeltaY, dy);

    g_posting_correction = true;
    tpsc_event_post(kCGHIDEventTap, event);
    g_posting_correction = false;
    CFRelease(event);
}

static void schedule_timer(void);

static void
timer_callback(CFRunLoopTimerRef timer, void *context)
{
    struct tpsc_vec correction;
    uint64_t now;
    uint64_t deadline;
    int status;

    (void)context;

    if (g_timer == timer) {
        CFRunLoopTimerInvalidate(g_timer);
        CFRelease(g_timer);
        g_timer = NULL;
    }

    if (!g_enabled || !g_filter ||
        !tpsc_rebound_filter_pending(g_filter))
        return;

    now = now_us();
    deadline = tpsc_rebound_filter_deadline_us(g_filter);
    if (deadline != 0 && now < deadline) {
        schedule_timer();
        return;
    }

    /*
     * If another physical pointer moved the cursor during the quiet interval,
     * do not surprise the user with a retrospective TrackPoint correction.
     */
    if (!cursor_still_at_last_trackpoint_position()) {
        tpsc_pointer_rebound_reset();
        return;
    }

    status = tpsc_rebound_filter_finish(g_filter, now, &correction);
    if (status != TPSC_OK) {
        fprintf(stderr,
                "trackpoint: rebound classifier finish failed: %d\n",
                status);
        tpsc_pointer_rebound_reset();
        return;
    }

    g_have_last_trackpoint_position = false;
    post_correction(correction);
}

static void
schedule_timer(void)
{
    CFRunLoopTimerContext context = {0, NULL, NULL, NULL, NULL};
    uint64_t deadline;
    CFAbsoluteTime fire_time;

    cancel_timer();

    if (!g_enabled || !g_filter)
        return;

    deadline = tpsc_rebound_filter_deadline_us(g_filter);
    if (deadline == 0)
        return;

    fire_time = (CFAbsoluteTime)((long double)deadline / 1000000.0L);
    g_timer = CFRunLoopTimerCreate(kCFAllocatorDefault,
                                   fire_time, 0.0, 0, 0,
                                   timer_callback, &context);
    if (!g_timer)
        return;

    CFRunLoopAddTimer(CFRunLoopGetMain(), g_timer, kCFRunLoopCommonModes);
}

int
tpsc_pointer_rebound_set_enabled(bool enabled)
{
    struct tpsc_rebound_config cfg;
    int status;

    if (enabled == g_enabled)
        return 0;

    cancel_timer();
    g_have_last_trackpoint_position = false;

    if (!enabled) {
        tpsc_rebound_filter_destroy(g_filter);
        g_filter = NULL;
        g_enabled = false;
        return 0;
    }

    tpsc_rebound_config_defaults(&cfg);
    g_filter = tpsc_rebound_filter_create(&cfg, &status);
    if (!g_filter) {
        fprintf(stderr,
                "trackpoint: cannot create rebound classifier: %d\n",
                status);
        g_enabled = false;
        return -1;
    }

    g_enabled = true;
    fprintf(stderr, "trackpoint: terminal rebound correction enabled\n");
    return 0;
}

void
tpsc_pointer_rebound_reset(void)
{
    cancel_timer();
    g_have_last_trackpoint_position = false;

    if (g_filter)
        tpsc_rebound_filter_reset(g_filter);
}

void
tpsc_pointer_rebound_observe(CGEventType type, CGEventRef event)
{
    struct tpsc_vec delta;
    uint64_t time_us;
    int status;

    if (!g_enabled || !g_filter || g_posting_correction)
        return;

    /* Dragging is semantically significant; never rewrite it retrospectively. */
    if (type != kCGEventMouseMoved) {
        tpsc_pointer_rebound_reset();
        return;
    }

    delta.x =
        (double)CGEventGetIntegerValueField(event, kCGMouseEventDeltaX);
    delta.y =
        (double)CGEventGetIntegerValueField(event, kCGMouseEventDeltaY);
    if (delta.x == 0.0 && delta.y == 0.0)
        return;

    time_us = now_us();
    status = tpsc_rebound_filter_feed(g_filter, time_us, delta);
    if (status != TPSC_OK) {
        fprintf(stderr,
                "trackpoint: rebound classifier feed failed: %d\n",
                status);
        tpsc_pointer_rebound_reset();
        return;
    }

    g_last_trackpoint_position = CGEventGetLocation(event);
    g_have_last_trackpoint_position = true;

    if (tpsc_rebound_filter_pending(g_filter))
        schedule_timer();
    else
        cancel_timer();
}
