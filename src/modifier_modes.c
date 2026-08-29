#include "modifier_modes.h"

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/* macOS virtual key codes for the two Shift keys and F14. PC Scroll Lock is
 * normally exposed as F14 by the macOS keyboard stack. */
#define KEYCODE_LEFT_SHIFT   56
#define KEYCODE_RIGHT_SHIFT  60
#define KEYCODE_SCROLL_LOCK 107

enum locked_axis {
    LOCKED_AXIS_NONE = 0,
    LOCKED_AXIS_HORIZONTAL,
    LOCKED_AXIS_VERTICAL,
};

static CFMachPortRef g_keyboard_tap;
static CFRunLoopSourceRef g_keyboard_tap_source;

static bool g_gesture_active;
static bool g_default_locked;
static bool g_gesture_locked;
static bool g_mode_change_pending;

static bool g_left_shift_down;
static bool g_right_shift_down;
static bool g_left_shift_consumed;
static bool g_right_shift_consumed;
static bool g_scroll_lock_down;

static enum locked_axis g_locked_axis;

static bool
key_is_down(CGKeyCode keycode)
{
    return CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState,
                                 keycode);
}

static void
reset_axis_lock(void)
{
    g_locked_axis = LOCKED_AXIS_NONE;
}

static void
toggle_active_mode(void)
{
    g_gesture_locked = !g_gesture_locked;
    reset_axis_lock();
    g_mode_change_pending = true;
}

static CGEventRef
keyboard_tap_callback(CGEventTapProxy proxy, CGEventType type,
                      CGEventRef event, void *context)
{
    CGKeyCode keycode;
    bool down;
    bool *known_down;
    bool *consumed;

    (void)proxy;
    (void)context;

    if (type == kCGEventTapDisabledByTimeout ||
        type == kCGEventTapDisabledByUserInput) {
        if (g_keyboard_tap)
            CGEventTapEnable(g_keyboard_tap, true);
        return event;
    }

    keycode = (CGKeyCode)CGEventGetIntegerValueField(event,
                                                      kCGKeyboardEventKeycode);

    if (type == kCGEventFlagsChanged &&
        (keycode == KEYCODE_LEFT_SHIFT || keycode == KEYCODE_RIGHT_SHIFT)) {
        down = key_is_down(keycode);
        if (keycode == KEYCODE_LEFT_SHIFT) {
            known_down = &g_left_shift_down;
            consumed = &g_left_shift_consumed;
        } else {
            known_down = &g_right_shift_down;
            consumed = &g_right_shift_consumed;
        }

        if (down == *known_down)
            return *consumed ? NULL : event;

        *known_down = down;

        if (down) {
            if (g_gesture_active) {
                toggle_active_mode();
                *consumed = true;
                return NULL;
            }
            return event;
        }

        if (*consumed) {
            *consumed = false;
            return NULL;
        }

        return event;
    }

    if ((type == kCGEventKeyDown || type == kCGEventKeyUp) &&
        keycode == KEYCODE_SCROLL_LOCK) {
        down = type == kCGEventKeyDown;

        if (down && !g_scroll_lock_down)
            g_default_locked = !g_default_locked;

        g_scroll_lock_down = down;

        /* Scroll Lock remains visible to applications and keyboard LEDs. */
        return event;
    }

    return event;
}

static void
modifier_cleanup(void)
{
    if (g_keyboard_tap_source) {
        CFRunLoopRemoveSource(CFRunLoopGetMain(), g_keyboard_tap_source,
                              kCFRunLoopCommonModes);
        CFRelease(g_keyboard_tap_source);
        g_keyboard_tap_source = NULL;
    }
    if (g_keyboard_tap) {
        CFRelease(g_keyboard_tap);
        g_keyboard_tap = NULL;
    }
}

__attribute__((constructor)) static void
modifier_init(void)
{
    CGEventMask mask;

    g_left_shift_down = key_is_down(KEYCODE_LEFT_SHIFT);
    g_right_shift_down = key_is_down(KEYCODE_RIGHT_SHIFT);
    g_scroll_lock_down = key_is_down(KEYCODE_SCROLL_LOCK);

    mask = CGEventMaskBit(kCGEventFlagsChanged) |
           CGEventMaskBit(kCGEventKeyDown) |
           CGEventMaskBit(kCGEventKeyUp);

    g_keyboard_tap = CGEventTapCreate(kCGSessionEventTap,
                                      kCGHeadInsertEventTap,
                                      kCGEventTapOptionDefault,
                                      mask,
                                      keyboard_tap_callback,
                                      NULL);
    if (!g_keyboard_tap)
        return;

    g_keyboard_tap_source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault,
                                                           g_keyboard_tap, 0);
    if (!g_keyboard_tap_source) {
        modifier_cleanup();
        return;
    }

    CFRunLoopAddSource(CFRunLoopGetMain(), g_keyboard_tap_source,
                       kCFRunLoopCommonModes);
    CGEventTapEnable(g_keyboard_tap, true);
    atexit(modifier_cleanup);
}

static void
gesture_begin(void)
{
    g_gesture_active = true;
    g_gesture_locked = g_default_locked;
    g_mode_change_pending = false;
    g_left_shift_consumed = false;
    g_right_shift_consumed = false;
    reset_axis_lock();
}

static void
gesture_end(void)
{
    g_gesture_active = false;
    g_mode_change_pending = false;
    g_left_shift_consumed = false;
    g_right_shift_consumed = false;
    reset_axis_lock();
}

static int
apply_pending_mode_change(struct tpsc_engine *engine, uint64_t time_us)
{
    if (!g_mode_change_pending)
        return TPSC_OK;

    g_mode_change_pending = false;
    reset_axis_lock();
    return tpsc_engine_restart(engine, time_us, TPSC_RESTART_BYPASS_STARTUP);
}

static void
filter_locked_output(struct tpsc_vec *output)
{
    double ax;
    double ay;

    if (!g_gesture_locked || !output)
        return;

    ax = fabs(output->x);
    ay = fabs(output->y);

    if (g_locked_axis == LOCKED_AXIS_NONE) {
        if (ax > ay)
            g_locked_axis = LOCKED_AXIS_HORIZONTAL;
        else if (ay > ax)
            g_locked_axis = LOCKED_AXIS_VERTICAL;
        else {
            /* A perfectly diagonal startup step contains no direction evidence.
             * Suppress it until a later sample breaks the tie. */
            output->x = 0.0;
            output->y = 0.0;
            return;
        }
    }

    if (g_locked_axis == LOCKED_AXIS_HORIZONTAL)
        output->y = 0.0;
    else
        output->x = 0.0;
}

int
tpsc_modifier_engine_begin(struct tpsc_engine *engine, uint64_t time_us)
{
    int status = tpsc_engine_begin(engine, time_us);

    if (status == TPSC_OK)
        gesture_begin();
    return status;
}

int
tpsc_modifier_engine_end(struct tpsc_engine *engine, uint64_t time_us)
{
    gesture_end();
    return tpsc_engine_end(engine, time_us);
}

int
tpsc_modifier_engine_feed(struct tpsc_engine *engine,
                          uint64_t time_us,
                          struct tpsc_vec delta)
{
    int status = apply_pending_mode_change(engine, time_us);

    if (status != TPSC_OK)
        return status;
    return tpsc_engine_feed(engine, time_us, delta);
}

int
tpsc_modifier_engine_tick(struct tpsc_engine *engine,
                          uint64_t time_us,
                          struct tpsc_vec *output)
{
    int status;

    if (g_mode_change_pending) {
        status = apply_pending_mode_change(engine, time_us);
        if (status != TPSC_OK)
            return status;
        if (output) {
            output->x = 0.0;
            output->y = 0.0;
        }
        return TPSC_OK;
    }

    status = tpsc_engine_tick(engine, time_us, output);
    if (status == TPSC_OK)
        filter_locked_output(output);
    return status;
}
