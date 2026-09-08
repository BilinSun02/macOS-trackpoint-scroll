#include "rebound_filter.h"

#include <CoreFoundation/CoreFoundation.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/*
 * Retrospective TrackPoint rebound correction.
 *
 * Reversal motion is NEVER delayed or suppressed. A reversal becomes a
 * candidate only after established motion in the opposite direction. If the
 * candidate then stops, it is classified as rebound only when it is BOTH:
 *
 *   - very short, AND
 *   - small in cumulative reverse displacement.
 *
 * This deliberately preserves slow, precise micro-corrections and large,
 * rapid intentional reversals. A confirmed rebound is undone retrospectively.
 * The correction is then overdriven very slightly in the pre-rebound direction
 * so an edge-bound pointer receives a fresh outward impulse after the undo.
 */
#define TPSC_REBOUND_ARM_SECONDS               0.400
#define TPSC_REBOUND_MIN_PRIOR_COUNTS          6.0
#define TPSC_REBOUND_SHORT_SECONDS             0.120
#define TPSC_REBOUND_SMALL_COUNTS              6.0
#define TPSC_REBOUND_OVERDRIVE_COUNTS          2
#define TPSC_REBOUND_DEFAULT_QUIET_SECONDS     0.140
#define TPSC_REBOUND_MIN_QUIET_SECONDS         0.100
#define TPSC_REBOUND_MAX_QUIET_SECONDS         0.700
#define TPSC_REBOUND_QUIET_GAP_MULTIPLIER      1.75
#define TPSC_REBOUND_MIN_REPORT_GAP_SECONDS    0.003
#define TPSC_REBOUND_MAX_REPORT_GAP_SECONDS    0.500
#define TPSC_REBOUND_RECENT_CAP_COUNTS         4096.0
#define TPSC_REBOUND_POSITION_SLOP_POINTS      1.0

struct rebound_axis {
    int established_direction;
    double established_counts;
    CFAbsoluteTime established_time;

    bool candidate;
    int candidate_direction;
    int64_t candidate_delta;
    double candidate_counts;
    CFAbsoluteTime candidate_start;
    CFAbsoluteTime candidate_last;
};

static struct rebound_axis g_x;
static struct rebound_axis g_y;
static CFRunLoopTimerRef g_timer;

static bool g_have_last_motion_time;
static CFAbsoluteTime g_last_motion_time;
static CFTimeInterval g_recent_report_gap;

static bool g_have_last_trackpoint_position;
static CGPoint g_last_trackpoint_position;
static bool g_posting_correction;

/* Implemented by event_shim.c. Calling the shim keeps its split-axis cursor
 * cache synchronized with the retrospective correction. */
void tpsc_event_post(CGEventTapLocation tap, CGEventRef event);

static int
sign_i64(int64_t value)
{
    return value > 0 ? 1 : -1;
}

static void
clear_axis(struct rebound_axis *axis)
{
    *axis = (struct rebound_axis){0};
}

static void
clear_candidate(struct rebound_axis *axis)
{
    axis->candidate = false;
    axis->candidate_direction = 0;
    axis->candidate_delta = 0;
    axis->candidate_counts = 0.0;
    axis->candidate_start = 0.0;
    axis->candidate_last = 0.0;
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

void
tpsc_rebound_filter_reset(void)
{
    cancel_timer();
    clear_axis(&g_x);
    clear_axis(&g_y);
    g_have_last_motion_time = false;
    g_last_motion_time = 0.0;
    g_recent_report_gap = 0.0;
    g_have_last_trackpoint_position = false;
}

static bool
have_candidate(void)
{
    return g_x.candidate || g_y.candidate;
}

static void
note_established_motion(struct rebound_axis *axis,
                        int direction, double counts, CFAbsoluteTime now)
{
    if (axis->established_direction == direction &&
        axis->established_time > 0.0 && now >= axis->established_time &&
        now - axis->established_time <= TPSC_REBOUND_ARM_SECONDS) {
        axis->established_counts += counts;
        if (axis->established_counts > TPSC_REBOUND_RECENT_CAP_COUNTS)
            axis->established_counts = TPSC_REBOUND_RECENT_CAP_COUNTS;
    } else {
        axis->established_direction = direction;
        axis->established_counts = counts;
    }

    axis->established_time = now;
}

static void
commit_candidate(struct rebound_axis *axis)
{
    int direction = axis->candidate_direction;
    double counts = axis->candidate_counts;
    CFAbsoluteTime time = axis->candidate_last;

    clear_candidate(axis);
    note_established_motion(axis, direction, counts, time);
}

static bool
candidate_is_rebound(const struct rebound_axis *axis)
{
    CFTimeInterval duration;

    if (!axis->candidate || axis->candidate_last < axis->candidate_start)
        return false;

    duration = axis->candidate_last - axis->candidate_start;
    return duration <= TPSC_REBOUND_SHORT_SECONDS &&
           axis->candidate_counts <= TPSC_REBOUND_SMALL_COUNTS;
}

static bool
candidate_has_become_intentional(const struct rebound_axis *axis)
{
    CFTimeInterval duration;

    if (!axis->candidate || axis->candidate_last < axis->candidate_start)
        return false;

    duration = axis->candidate_last - axis->candidate_start;

    /* Complement of the rebound AND rule: either threshold is enough to prove
     * that the live reversal is no longer the small-and-quick rebound shape. */
    return duration > TPSC_REBOUND_SHORT_SECONDS ||
           axis->candidate_counts > TPSC_REBOUND_SMALL_COUNTS;
}

static void
accumulate_candidate_delta(struct rebound_axis *axis, int64_t delta)
{
    if (delta > 0 && axis->candidate_delta > INT64_MAX - delta)
        axis->candidate_delta = INT64_MAX;
    else if (delta < 0 && axis->candidate_delta < INT64_MIN - delta)
        axis->candidate_delta = INT64_MIN;
    else
        axis->candidate_delta += delta;
}

static void
observe_axis(struct rebound_axis *axis, int64_t delta, CFAbsoluteTime now)
{
    int direction;
    double counts;

    if (delta == 0)
        return;

    direction = sign_i64(delta);
    counts = fabs((double)delta);

    if (axis->candidate) {
        if (direction == axis->candidate_direction) {
            accumulate_candidate_delta(axis, delta);
            axis->candidate_counts += counts;
            axis->candidate_last = now;

            if (candidate_has_become_intentional(axis))
                commit_candidate(axis);
            return;
        }

        /* A second reversal before idle is not a terminal rebound episode. */
        clear_candidate(axis);
        note_established_motion(axis, direction, counts, now);
        return;
    }

    if (axis->established_direction != 0 &&
        direction != axis->established_direction &&
        axis->established_time > 0.0 && now >= axis->established_time &&
        now - axis->established_time <= TPSC_REBOUND_ARM_SECONDS &&
        axis->established_counts >= TPSC_REBOUND_MIN_PRIOR_COUNTS) {
        axis->candidate = true;
        axis->candidate_direction = direction;
        axis->candidate_delta = delta;
        axis->candidate_counts = counts;
        axis->candidate_start = now;
        axis->candidate_last = now;
        return;
    }

    note_established_motion(axis, direction, counts, now);
}

static CFTimeInterval
quiet_interval(void)
{
    CFTimeInterval quiet = TPSC_REBOUND_DEFAULT_QUIET_SECONDS;

    if (g_recent_report_gap > 0.0)
        quiet = g_recent_report_gap * TPSC_REBOUND_QUIET_GAP_MULTIPLIER;

    if (quiet < TPSC_REBOUND_MIN_QUIET_SECONDS)
        quiet = TPSC_REBOUND_MIN_QUIET_SECONDS;
    if (quiet > TPSC_REBOUND_MAX_QUIET_SECONDS)
        quiet = TPSC_REBOUND_MAX_QUIET_SECONDS;
    return quiet;
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
rebound_correction_delta(int64_t candidate_delta)
{
    int64_t undo;
    int64_t overdrive = TPSC_REBOUND_OVERDRIVE_COUNTS;

    if (candidate_delta == 0)
        return 0;

    if (candidate_delta == INT64_MIN)
        undo = INT64_MAX;
    else
        undo = -candidate_delta;

    if (undo > 0) {
        if (undo > INT64_MAX - overdrive)
            return INT64_MAX;
        return undo + overdrive;
    }

    if (undo < INT64_MIN + overdrive)
        return INT64_MIN;
    return undo - overdrive;
}

static void
post_correction(int64_t dx, int64_t dy)
{
    CGEventRef event;
    CGPoint position;

    if (dx == 0 && dy == 0)
        return;
    if (!current_cursor_position(&position))
        return;

    position.x += (double)dx;
    position.y += (double)dy;

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

static void
finish_idle_episode(void)
{
    int64_t rebound_x = 0;
    int64_t rebound_y = 0;
    int64_t correction_x = 0;
    int64_t correction_y = 0;

    if (candidate_is_rebound(&g_x)) {
        rebound_x = g_x.candidate_delta;
        correction_x = rebound_correction_delta(rebound_x);
    }
    if (candidate_is_rebound(&g_y)) {
        rebound_y = g_y.candidate_delta;
        correction_y = rebound_correction_delta(rebound_y);
    }

    /* Idle is a hard motion boundary. A later movement starts fresh rather
     * than inheriting the pre-idle direction as a new rebound reference. */
    clear_axis(&g_x);
    clear_axis(&g_y);
    g_have_last_motion_time = false;
    g_last_motion_time = 0.0;
    g_have_last_trackpoint_position = false;

    if (correction_x != 0 || correction_y != 0) {
        fprintf(stderr,
                "trackpoint: rebound measured dx=%" PRId64 " dy=%" PRId64
                " correction dx=%" PRId64 " dy=%" PRId64 "\n",
                rebound_x, rebound_y, correction_x, correction_y);
        post_correction(correction_x, correction_y);
    }
}

static void
rebound_timer_callback(CFRunLoopTimerRef timer, void *context)
{
    CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
    CFTimeInterval quiet = quiet_interval();

    (void)context;

    if (g_timer == timer) {
        CFRunLoopTimerInvalidate(g_timer);
        CFRelease(g_timer);
        g_timer = NULL;
    }

    if (!have_candidate() || !g_have_last_motion_time)
        return;

    if (now < g_last_motion_time || now - g_last_motion_time < quiet)
        return;

    /* If another physical pointer moved the cursor during our quiet interval,
     * do not surprise the user by applying a retrospective TrackPoint move. */
    if (!cursor_still_at_last_trackpoint_position()) {
        tpsc_rebound_filter_reset();
        return;
    }

    finish_idle_episode();
}

static void
schedule_timer(void)
{
    CFRunLoopTimerContext context = {0, NULL, NULL, NULL, NULL};
    CFAbsoluteTime fire_time;

    cancel_timer();
    if (!have_candidate() || !g_have_last_motion_time)
        return;

    fire_time = g_last_motion_time + quiet_interval();
    g_timer = CFRunLoopTimerCreate(kCFAllocatorDefault,
                                   fire_time, 0.0, 0, 0,
                                   rebound_timer_callback, &context);
    if (!g_timer)
        return;

    CFRunLoopAddTimer(CFRunLoopGetMain(), g_timer, kCFRunLoopCommonModes);
}

void
tpsc_rebound_filter_observe(CGEventType type, CGEventRef event)
{
    CFAbsoluteTime now;
    int64_t dx;
    int64_t dy;

    if (g_posting_correction)
        return;

    /* Dragging is semantically significant; never rewrite it retrospectively. */
    if (type != kCGEventMouseMoved) {
        tpsc_rebound_filter_reset();
        return;
    }

    dx = CGEventGetIntegerValueField(event, kCGMouseEventDeltaX);
    dy = CGEventGetIntegerValueField(event, kCGMouseEventDeltaY);
    if (dx == 0 && dy == 0)
        return;

    now = CFAbsoluteTimeGetCurrent();

    if (g_have_last_motion_time && now >= g_last_motion_time) {
        CFTimeInterval gap = now - g_last_motion_time;

        /* X/Y callbacks from one physical report can be nearly simultaneous.
         * Ignore those when estimating the device's sparse report cadence. */
        if (gap >= TPSC_REBOUND_MIN_REPORT_GAP_SECONDS &&
            gap <= TPSC_REBOUND_MAX_REPORT_GAP_SECONDS) {
            if (g_recent_report_gap <= 0.0 || gap > g_recent_report_gap)
                g_recent_report_gap = gap;
            else
                g_recent_report_gap *= 0.92;
        }
    }

    g_last_motion_time = now;
    g_have_last_motion_time = true;
    g_last_trackpoint_position = CGEventGetLocation(event);
    g_have_last_trackpoint_position = true;

    observe_axis(&g_x, dx, now);
    observe_axis(&g_y, dy, now);
    schedule_timer();
}
