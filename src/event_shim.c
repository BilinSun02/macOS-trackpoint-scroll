#undef CGEventGetLocation
#undef CGEventPost

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "config.h"

#define POINTER_REPORT_MERGE_NS 2000000ULL
#define POINTER_IDLE_RESET_NS 333333000ULL

static bool g_have_posted_pointer_position;
static CGPoint g_posted_pointer_position;
static CFAbsoluteTime g_posted_pointer_time;

static bool g_pointer_config_loaded;
static double g_pointer_speed = 1.0;
static double g_pointer_acceleration;
static double g_pointer_acceleration_velocity = 0.10;
static double g_pointer_gain = 1.0;
static double g_pointer_remainder_x;
static double g_pointer_remainder_y;

static bool g_have_pointer_report;
static CGEventTimestamp g_pointer_report_time;
static double g_pointer_report_dx;
static double g_pointer_report_dy;

static bool
is_pointer_motion(CGEventType type)
{
    return type == kCGEventMouseMoved ||
           type == kCGEventLeftMouseDragged ||
           type == kCGEventRightMouseDragged ||
           type == kCGEventOtherMouseDragged;
}

static void
load_pointer_config_once(void)
{
    struct macos_trackpoint_config cfg;
    char path[1024];
    const char *selected;

    if (g_pointer_config_loaded)
        return;
    g_pointer_config_loaded = true;

    macos_trackpoint_config_defaults(&cfg);
    selected = macos_trackpoint_default_config_path(path, sizeof(path));
    if (selected && macos_trackpoint_config_load(&cfg, selected, false) == 0) {
        g_pointer_speed = cfg.pointer_speed;
        g_pointer_acceleration = cfg.pointer_acceleration;
        g_pointer_acceleration_velocity = cfg.pointer_acceleration_velocity;
    }
    g_pointer_gain = g_pointer_speed;

    fprintf(stderr,
            "trackpoint: pointer curve speed=%g acceleration=%g velocity=%g counts/ms\n",
            g_pointer_speed, g_pointer_acceleration,
            g_pointer_acceleration_velocity);
}

static void
reset_pointer_motion_state(void)
{
    g_have_pointer_report = false;
    g_pointer_report_time = 0;
    g_pointer_report_dx = 0.0;
    g_pointer_report_dy = 0.0;
    g_pointer_gain = g_pointer_speed;
    g_pointer_remainder_x = 0.0;
    g_pointer_remainder_y = 0.0;
}

static double
clamp_gain(double gain)
{
    if (!isfinite(gain) || gain < 0.0)
        return 0.0;
    if (gain > 64.0)
        return 64.0;
    return gain;
}

static void
start_pointer_report(CGEventTimestamp time)
{
    uint64_t dt_ns;
    double speed;
    double response;

    if (g_have_pointer_report && time > g_pointer_report_time) {
        dt_ns = (uint64_t)(time - g_pointer_report_time);

        if (dt_ns > POINTER_IDLE_RESET_NS || g_pointer_acceleration == 0.0) {
            g_pointer_gain = g_pointer_speed;
        } else {
            speed = hypot(g_pointer_report_dx, g_pointer_report_dy) *
                    1000000.0 / (double)dt_ns;
            if (speed <= 0.0) {
                g_pointer_gain = g_pointer_speed;
            } else {
                response = speed / (speed + g_pointer_acceleration_velocity);
                g_pointer_gain = clamp_gain(g_pointer_speed *
                                             (1.0 + g_pointer_acceleration *
                                                    response));
            }
        }
    } else {
        g_pointer_gain = g_pointer_speed;
    }

    g_pointer_report_time = time;
    g_pointer_report_dx = 0.0;
    g_pointer_report_dy = 0.0;
    g_have_pointer_report = true;
}

static void
account_pointer_report(CGEventTimestamp time, int64_t dx, int64_t dy)
{
    if (!g_have_pointer_report || time < g_pointer_report_time ||
        (uint64_t)(time - g_pointer_report_time) > POINTER_REPORT_MERGE_NS)
        start_pointer_report(time);

    g_pointer_report_dx += (double)dx;
    g_pointer_report_dy += (double)dy;
}

static int64_t
scale_axis(int64_t delta, double gain, double *remainder)
{
    double total;
    double integral;

    if (gain == 1.0)
        return delta;

    total = (double)delta * gain + *remainder;
    integral = trunc(total);

    if (integral > (double)INT64_MAX)
        integral = (double)INT64_MAX;
    else if (integral < (double)INT64_MIN)
        integral = (double)INT64_MIN;

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
    CGEventTimestamp time;

    load_pointer_config_once();

    raw_dx = CGEventGetIntegerValueField(event, kCGMouseEventDeltaX);
    raw_dy = CGEventGetIntegerValueField(event, kCGMouseEventDeltaY);
    if (raw_dx == 0 && raw_dy == 0)
        return;

    if (g_pointer_speed == 1.0 && g_pointer_acceleration == 0.0)
        return;

    time = CGEventGetTimestamp(event);
    account_pointer_report(time, raw_dx, raw_dy);

    out_dx = scale_axis(raw_dx, g_pointer_gain, &g_pointer_remainder_x);
    out_dy = scale_axis(raw_dy, g_pointer_gain, &g_pointer_remainder_y);

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
    CGEventType type = CGEventGetType(event);

    if (type == kCGEventScrollWheel && g_pointer_config_loaded)
        reset_pointer_motion_state();

    if (is_pointer_motion(type)) {
        scale_pointer_event(event);
        g_posted_pointer_position = CGEventGetLocation(event);
        g_posted_pointer_time = CFAbsoluteTimeGetCurrent();
        g_have_posted_pointer_position = true;
    }

    CGEventPost(tap, event);
}
