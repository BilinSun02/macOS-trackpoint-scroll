#include "modifier_modes.h"

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/hid/IOHIDUsageTables.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* USB HID keyboard usage page/usages. */
#define HID_PAGE_KEYBOARD       0x07
#define HID_USAGE_SCROLL_LOCK   0x47
#define HID_USAGE_LEFT_SHIFT    0xe1
#define HID_USAGE_RIGHT_SHIFT   0xe5

/* macOS virtual key codes used only for consuming Shift from applications. */
#define KEYCODE_LEFT_SHIFT      56
#define KEYCODE_RIGHT_SHIFT     60

enum locked_axis {
    LOCKED_AXIS_NONE = 0,
    LOCKED_AXIS_HORIZONTAL,
    LOCKED_AXIS_VERTICAL,
};

static IOHIDManagerRef g_keyboard_hid_manager;
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
    fprintf(stderr, "trackpoint: Shift toggled scroll mode -> %s\n",
            g_gesture_locked ? "locked" : "free");
}

static CFMutableDictionaryRef
keyboard_matching_dictionary(void)
{
    CFMutableDictionaryRef dict;
    CFNumberRef usage_page;
    CFNumberRef usage;
    int up = kHIDPage_GenericDesktop;
    int u = kHIDUsage_GD_Keyboard;

    dict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                     &kCFTypeDictionaryKeyCallBacks,
                                     &kCFTypeDictionaryValueCallBacks);
    if (!dict)
        return NULL;

    usage_page = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &up);
    usage = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &u);
    if (!usage_page || !usage) {
        if (usage_page)
            CFRelease(usage_page);
        if (usage)
            CFRelease(usage);
        CFRelease(dict);
        return NULL;
    }

    CFDictionarySetValue(dict, CFSTR(kIOHIDPrimaryUsagePageKey), usage_page);
    CFDictionarySetValue(dict, CFSTR(kIOHIDPrimaryUsageKey), usage);
    CFRelease(usage_page);
    CFRelease(usage);
    return dict;
}

static void
keyboard_input_value(void *context, IOReturn result, void *sender,
                     IOHIDValueRef value)
{
    IOHIDElementRef element;
    uint32_t page;
    uint32_t usage;
    bool down;
    bool *known_down;
    bool *consumed;

    (void)context;
    (void)result;
    (void)sender;

    element = IOHIDValueGetElement(value);
    if (!element)
        return;

    page = IOHIDElementGetUsagePage(element);
    usage = IOHIDElementGetUsage(element);
    if (page != HID_PAGE_KEYBOARD)
        return;

    down = IOHIDValueGetIntegerValue(value) != 0;

    if (usage == HID_USAGE_LEFT_SHIFT || usage == HID_USAGE_RIGHT_SHIFT) {
        if (usage == HID_USAGE_LEFT_SHIFT) {
            known_down = &g_left_shift_down;
            consumed = &g_left_shift_consumed;
        } else {
            known_down = &g_right_shift_down;
            consumed = &g_right_shift_consumed;
        }

        if (down == *known_down)
            return;
        *known_down = down;

        /* A Shift already held before middle-down does nothing. Only a fresh
         * physical press while the gesture is active toggles mode. */
        if (down && g_gesture_active) {
            toggle_active_mode();
            *consumed = true;
        }
        return;
    }

    if (usage == HID_USAGE_SCROLL_LOCK) {
        if (down && !g_scroll_lock_down) {
            g_default_locked = !g_default_locked;
            fprintf(stderr, "trackpoint: Scroll Lock default mode -> %s\n",
                    g_default_locked ? "locked" : "free");
        }
        g_scroll_lock_down = down;
    }
}

static CGEventRef
keyboard_tap_callback(CGEventTapProxy proxy, CGEventType type,
                      CGEventRef event, void *context)
{
    CGKeyCode keycode;
    CGEventFlags flags;
    bool down;
    bool *consumed;

    (void)proxy;
    (void)context;

    if (type == kCGEventTapDisabledByTimeout ||
        type == kCGEventTapDisabledByUserInput) {
        if (g_keyboard_tap)
            CGEventTapEnable(g_keyboard_tap, true);
        return event;
    }

    if (type != kCGEventFlagsChanged)
        return event;

    keycode = (CGKeyCode)CGEventGetIntegerValueField(event,
                                                      kCGKeyboardEventKeycode);
    if (keycode == KEYCODE_LEFT_SHIFT)
        consumed = &g_left_shift_consumed;
    else if (keycode == KEYCODE_RIGHT_SHIFT)
        consumed = &g_right_shift_consumed;
    else
        return event;

    flags = CGEventGetFlags(event);
    down = (flags & kCGEventFlagMaskShift) != 0;

    /* Raw HID owns the behavior. This tap exists only to preserve the Linux
     * convention that the Shift which toggles an active scroll gesture does
     * not leak into applications. It is deliberately nonessential: if a sudo
     * launch cannot create this session tap, modifier behavior still works. */
    if (down && g_gesture_active) {
        *consumed = true;
        return NULL;
    }

    if (!down && *consumed) {
        *consumed = false;
        return NULL;
    }

    return event;
}

static void
modifier_cleanup(void)
{
    if (g_keyboard_hid_manager) {
        IOHIDManagerUnscheduleFromRunLoop(g_keyboard_hid_manager,
                                          CFRunLoopGetMain(),
                                          kCFRunLoopCommonModes);
        IOHIDManagerClose(g_keyboard_hid_manager, kIOHIDOptionsTypeNone);
        CFRelease(g_keyboard_hid_manager);
        g_keyboard_hid_manager = NULL;
    }

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

static bool
setup_keyboard_hid(void)
{
    CFMutableDictionaryRef match;
    IOReturn status;

    g_keyboard_hid_manager = IOHIDManagerCreate(kCFAllocatorDefault,
                                                 kIOHIDOptionsTypeNone);
    if (!g_keyboard_hid_manager)
        return false;

    match = keyboard_matching_dictionary();
    if (!match)
        return false;

    IOHIDManagerSetDeviceMatching(g_keyboard_hid_manager, match);
    CFRelease(match);
    IOHIDManagerRegisterInputValueCallback(g_keyboard_hid_manager,
                                            keyboard_input_value, NULL);
    IOHIDManagerScheduleWithRunLoop(g_keyboard_hid_manager,
                                    CFRunLoopGetMain(),
                                    kCFRunLoopCommonModes);

    status = IOHIDManagerOpen(g_keyboard_hid_manager, kIOHIDOptionsTypeNone);
    if (status != kIOReturnSuccess) {
        fprintf(stderr,
                "trackpoint: keyboard HID monitor open failed: 0x%08x\n",
                status);
        return false;
    }

    return true;
}

static void
setup_optional_shift_tap(void)
{
    CGEventMask mask = CGEventMaskBit(kCGEventFlagsChanged);

    g_keyboard_tap = CGEventTapCreate(kCGSessionEventTap,
                                      kCGHeadInsertEventTap,
                                      kCGEventTapOptionDefault,
                                      mask,
                                      keyboard_tap_callback,
                                      NULL);
    if (!g_keyboard_tap) {
        fprintf(stderr,
                "trackpoint: note: cannot create Shift-consumption event tap; "
                "modifier modes still work via raw HID\n");
        return;
    }

    g_keyboard_tap_source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault,
                                                           g_keyboard_tap, 0);
    if (!g_keyboard_tap_source) {
        CFRelease(g_keyboard_tap);
        g_keyboard_tap = NULL;
        return;
    }

    CFRunLoopAddSource(CFRunLoopGetMain(), g_keyboard_tap_source,
                       kCFRunLoopCommonModes);
    CGEventTapEnable(g_keyboard_tap, true);
}

__attribute__((constructor)) static void
modifier_init(void)
{
    if (!setup_keyboard_hid()) {
        modifier_cleanup();
        fprintf(stderr,
                "trackpoint: warning: raw keyboard modifier monitor unavailable\n");
        return;
    }

    setup_optional_shift_tap();
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
