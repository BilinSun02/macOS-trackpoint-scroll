#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDLib.h>
#include <IOKit/hid/IOHIDUsageTables.h>
#include <crt_externs.h>
#include <mach/mach_time.h>

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "pointer_rebound.h"

#define POINTER_IDLE_RESET_US 333333ULL
#define MIDDLE_BUTTON_USAGE 3u
#define MAX_POINTER_GAIN 64.0

struct pointer_state {
    bool config_loaded;
    bool middle_down;
    bool have_report;

    double base_speed;
    double acceleration;
    double velocity_scale;
    double gain;
    double remainder_x;
    double remainder_y;

    uint64_t report_time_ticks;
    double report_dx;
    double report_dy;
};

static struct pointer_state g_pointer = {
    .base_speed = 1.0,
    .velocity_scale = 0.10,
    .gain = 1.0,
};

static mach_timebase_info_data_t g_timebase;

static uint64_t
ticks_delta_to_us(uint64_t ticks)
{
    long double nanos;

    if (g_timebase.denom == 0)
        (void)mach_timebase_info(&g_timebase);

    nanos = (long double)ticks * (long double)g_timebase.numer /
            (long double)g_timebase.denom;
    return (uint64_t)(nanos / 1000.0L);
}

static const char *
config_path_from_process_args(char *buffer, unsigned long size)
{
    const char *path = macos_trackpoint_default_config_path(buffer, size);
    int argc = *_NSGetArgc();
    char **argv = *_NSGetArgv();
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc)
            path = argv[++i];
    }
    return path;
}

static void
load_config_once(void)
{
    struct macos_trackpoint_config cfg;
    char path[1024];
    const char *selected;

    if (g_pointer.config_loaded)
        return;
    g_pointer.config_loaded = true;

    macos_trackpoint_config_defaults(&cfg);
    selected = config_path_from_process_args(path, sizeof(path));
    if (selected && macos_trackpoint_config_load(&cfg, selected, false) == 0) {
        g_pointer.base_speed = cfg.pointer_speed;
        g_pointer.acceleration = cfg.pointer_acceleration;
        g_pointer.velocity_scale = cfg.pointer_acceleration_velocity;
    }
    g_pointer.gain = g_pointer.base_speed;

    fprintf(stderr,
            "trackpoint: raw HID pointer curve speed=%g acceleration=%g "
            "velocity=%g counts/ms\n",
            g_pointer.base_speed, g_pointer.acceleration,
            g_pointer.velocity_scale);
}

static void
reset_motion_state(void)
{
    g_pointer.have_report = false;
    g_pointer.report_time_ticks = 0;
    g_pointer.report_dx = 0.0;
    g_pointer.report_dy = 0.0;
    g_pointer.gain = g_pointer.base_speed;
    g_pointer.remainder_x = 0.0;
    g_pointer.remainder_y = 0.0;
}

static double
clamp_gain(double gain)
{
    if (!isfinite(gain) || gain < 0.0)
        return 0.0;
    if (gain > MAX_POINTER_GAIN)
        return MAX_POINTER_GAIN;
    return gain;
}

static void
start_report(uint64_t time_ticks)
{
    if (g_pointer.have_report && time_ticks > g_pointer.report_time_ticks) {
        uint64_t dt_us = ticks_delta_to_us(time_ticks - g_pointer.report_time_ticks);

        if (dt_us == 0 || dt_us > POINTER_IDLE_RESET_US) {
            g_pointer.gain = g_pointer.base_speed;
            if (dt_us > POINTER_IDLE_RESET_US) {
                g_pointer.remainder_x = 0.0;
                g_pointer.remainder_y = 0.0;
            }
        } else if (g_pointer.acceleration == 0.0) {
            g_pointer.gain = g_pointer.base_speed;
        } else {
            double speed = hypot(g_pointer.report_dx, g_pointer.report_dy) *
                           1000.0 / (double)dt_us;

            if (speed <= 0.0) {
                g_pointer.gain = g_pointer.base_speed;
            } else {
                double response = speed / (speed + g_pointer.velocity_scale);
                g_pointer.gain = clamp_gain(g_pointer.base_speed *
                                             (1.0 + g_pointer.acceleration *
                                                    response));
            }
        }
    } else {
        g_pointer.gain = g_pointer.base_speed;
        if (g_pointer.have_report && time_ticks < g_pointer.report_time_ticks) {
            g_pointer.remainder_x = 0.0;
            g_pointer.remainder_y = 0.0;
        }
    }

    g_pointer.report_time_ticks = time_ticks;
    g_pointer.report_dx = 0.0;
    g_pointer.report_dy = 0.0;
    g_pointer.have_report = true;
}

static int64_t
scale_axis(int64_t delta, double *remainder)
{
    double total;
    double integral;

    if (g_pointer.gain == 1.0 && *remainder == 0.0)
        return delta;

    total = (double)delta * g_pointer.gain + *remainder;
    integral = trunc(total);

    if (integral > (double)INT64_MAX)
        integral = (double)INT64_MAX;
    else if (integral < (double)INT64_MIN)
        integral = (double)INT64_MIN;

    *remainder = total - integral;
    return (int64_t)integral;
}

static int64_t
transform_motion(IOHIDValueRef value, uint32_t usage, int64_t raw)
{
    uint64_t time_ticks;
    double *remainder;

    if (g_pointer.middle_down)
        return raw;

    /* Preserve the original forwarding path exactly at the default curve. */
    if (g_pointer.base_speed == 1.0 && g_pointer.acceleration == 0.0)
        return raw;

    time_ticks = IOHIDValueGetTimeStamp(value);
    if (!g_pointer.have_report || time_ticks != g_pointer.report_time_ticks)
        start_report(time_ticks);

    if (usage == kHIDUsage_GD_X) {
        g_pointer.report_dx += (double)raw;
        remainder = &g_pointer.remainder_x;
    } else {
        g_pointer.report_dy += (double)raw;
        remainder = &g_pointer.remainder_y;
    }

    /* A zero hardware delta must remain zero; carry fractional remainder until
     * that axis actually moves again instead of manufacturing idle motion. */
    if (raw == 0)
        return 0;

    return scale_axis(raw, remainder);
}

CFIndex
tpsc_pointer_value_get_integer_value(IOHIDValueRef value)
{
    IOHIDElementRef element;
    uint32_t page;
    uint32_t usage;
    CFIndex native_value;
    int64_t transformed;

    native_value = IOHIDValueGetIntegerValue(value);
    load_config_once();

    element = IOHIDValueGetElement(value);
    if (!element)
        return native_value;

    page = IOHIDElementGetUsagePage(element);
    usage = IOHIDElementGetUsage(element);

    if (page == kHIDPage_Button && usage == MIDDLE_BUTTON_USAGE) {
        bool down = native_value != 0;

        if (down != g_pointer.middle_down) {
            g_pointer.middle_down = down;
            reset_motion_state();
            tpsc_pointer_rebound_reset();
        }
        return native_value;
    }

    if (page != kHIDPage_GenericDesktop ||
        (usage != kHIDUsage_GD_X && usage != kHIDUsage_GD_Y))
        return native_value;

    transformed = transform_motion(value, usage, (int64_t)native_value);
    if (transformed > (int64_t)LONG_MAX)
        return (CFIndex)LONG_MAX;
    if (transformed < (int64_t)LONG_MIN)
        return (CFIndex)LONG_MIN;
    return (CFIndex)transformed;
}
