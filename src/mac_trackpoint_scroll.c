#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/hid/IOHIDUsageTables.h>
#include <mach/mach_time.h>

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "trackpoint_scroll/engine.h"
#include "trackpoint_scroll/profiles.h"

#define DEFAULT_VENDOR_ID  0x5859
#define DEFAULT_PRODUCT_ID 0x0001
#define MIDDLE_BUTTON_USAGE 3

struct app {
    IOHIDManagerRef hid_manager;
    CFMachPortRef event_tap;
    CFRunLoopSourceRef event_tap_source;
    CFRunLoopTimerRef tick_timer;

    struct tpsc_profile profile;
    struct tpsc_engine *engine;

    uint32_t vendor_id;
    uint32_t product_id;
    double scroll_scale;
    double x_sign;
    double y_sign;
    double point_remainder_x;
    double point_remainder_y;

    bool seize;
    bool verbose;
    bool suppress_middle_click;
    bool middle_down;
    bool left_down;
    bool right_down;
    bool target_present;
};

static mach_timebase_info_data_t g_timebase;

static uint64_t
mach_ticks_to_us(uint64_t ticks)
{
    long double nanos = (long double)ticks * (long double)g_timebase.numer /
                        (long double)g_timebase.denom;
    return (uint64_t)(nanos / 1000.0L);
}

static uint64_t
now_us(void)
{
    return mach_ticks_to_us(mach_absolute_time());
}

static void
usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s [options]\n"
            "  --vendor HEX            HID vendor id (default 5859)\n"
            "  --product HEX           HID product id (default 0001)\n"
            "  --scroll-scale N        macOS pixel scaling (default 8.0)\n"
            "  --invert-x              reverse horizontal scroll direction\n"
            "  --invert-y              reverse vertical scroll direction\n"
            "  --allow-middle-click    do not suppress Quartz middle clicks\n"
            "  --seize                 exclusively claim the HID device\n"
            "  --verbose               print device/gesture diagnostics\n"
            "  --help                  show this text\n",
            argv0);
}

static bool
parse_u32_hex(const char *s, uint32_t *out)
{
    char *end = NULL;
    unsigned long value;

    errno = 0;
    value = strtoul(s, &end, 16);
    if (errno || !end || *end != '\0' || value > UINT32_MAX)
        return false;
    *out = (uint32_t)value;
    return true;
}

static bool
parse_double(const char *s, double *out)
{
    char *end = NULL;
    double value;

    errno = 0;
    value = strtod(s, &end);
    if (errno || !end || *end != '\0' || !isfinite(value))
        return false;
    *out = value;
    return true;
}

static int
parse_args(struct app *app, int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            exit(0);
        } else if (strcmp(argv[i], "--verbose") == 0) {
            app->verbose = true;
        } else if (strcmp(argv[i], "--seize") == 0) {
            app->seize = true;
        } else if (strcmp(argv[i], "--allow-middle-click") == 0) {
            app->suppress_middle_click = false;
        } else if (strcmp(argv[i], "--invert-x") == 0) {
            app->x_sign *= -1.0;
        } else if (strcmp(argv[i], "--invert-y") == 0) {
            app->y_sign *= -1.0;
        } else if (strcmp(argv[i], "--vendor") == 0 && i + 1 < argc) {
            if (!parse_u32_hex(argv[++i], &app->vendor_id))
                return -1;
        } else if (strcmp(argv[i], "--product") == 0 && i + 1 < argc) {
            if (!parse_u32_hex(argv[++i], &app->product_id))
                return -1;
        } else if (strcmp(argv[i], "--scroll-scale") == 0 && i + 1 < argc) {
            if (!parse_double(argv[++i], &app->scroll_scale) ||
                app->scroll_scale <= 0.0)
                return -1;
        } else {
            return -1;
        }
    }

    return 0;
}

static CFMutableDictionaryRef
matching_dictionary(const struct app *app)
{
    CFMutableDictionaryRef dict;
    CFNumberRef vendor;
    CFNumberRef product;
    CFNumberRef usage_page;
    CFNumberRef usage;
    int v = (int)app->vendor_id;
    int p = (int)app->product_id;
    int up = kHIDPage_GenericDesktop;
    int u = kHIDUsage_GD_Mouse;

    dict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                     &kCFTypeDictionaryKeyCallBacks,
                                     &kCFTypeDictionaryValueCallBacks);
    if (!dict)
        return NULL;

    vendor = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &v);
    product = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &p);
    usage_page = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &up);
    usage = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &u);
    if (!vendor || !product || !usage_page || !usage) {
        if (vendor) CFRelease(vendor);
        if (product) CFRelease(product);
        if (usage_page) CFRelease(usage_page);
        if (usage) CFRelease(usage);
        CFRelease(dict);
        return NULL;
    }

    CFDictionarySetValue(dict, CFSTR(kIOHIDVendorIDKey), vendor);
    CFDictionarySetValue(dict, CFSTR(kIOHIDProductIDKey), product);
    CFDictionarySetValue(dict, CFSTR(kIOHIDPrimaryUsagePageKey), usage_page);
    CFDictionarySetValue(dict, CFSTR(kIOHIDPrimaryUsageKey), usage);

    CFRelease(vendor);
    CFRelease(product);
    CFRelease(usage_page);
    CFRelease(usage);
    return dict;
}

static void
print_cf_string_property(IOHIDDeviceRef device, CFStringRef key)
{
    CFTypeRef value = IOHIDDeviceGetProperty(device, key);
    char buffer[512];

    if (!value || CFGetTypeID(value) != CFStringGetTypeID())
        return;
    if (CFStringGetCString((CFStringRef)value, buffer, sizeof(buffer),
                           kCFStringEncodingUTF8))
        fprintf(stderr, "%s", buffer);
}

static void
device_matched(void *context, IOReturn result, void *sender,
               IOHIDDeviceRef device)
{
    struct app *app = context;
    (void)result;
    (void)sender;

    app->target_present = true;
    fprintf(stderr, "trackpoint: matched HID device ");
    print_cf_string_property(device, CFSTR(kIOHIDProductKey));
    fprintf(stderr, " (vid=%04x pid=%04x)%s\n",
            app->vendor_id, app->product_id,
            app->seize ? " [seized]" : "");
}

static void
device_removed(void *context, IOReturn result, void *sender,
               IOHIDDeviceRef device)
{
    struct app *app = context;
    (void)result;
    (void)sender;
    (void)device;

    app->target_present = false;
    app->middle_down = false;
    app->left_down = false;
    app->right_down = false;
    app->point_remainder_x = 0.0;
    app->point_remainder_y = 0.0;
    (void)tpsc_engine_end(app->engine, now_us());
    fprintf(stderr, "trackpoint: target HID device removed\n");
}

static int32_t
take_point_delta(double value, double *remainder)
{
    double total = *remainder + value;
    double integral = trunc(total);

    if (integral > (double)INT32_MAX)
        integral = (double)INT32_MAX;
    else if (integral < (double)INT32_MIN)
        integral = (double)INT32_MIN;

    *remainder = total - integral;
    return (int32_t)integral;
}

static void
post_scroll(struct app *app, double vertical, double horizontal)
{
    CGEventRef event;
    int32_t point_vertical;
    int32_t point_horizontal;

    if (vertical == 0.0 && horizontal == 0.0)
        return;

    /*
     * CoreGraphics exposes the same smooth-scroll displacement in multiple
     * representations. Some applications consume the 16.16 fixed-point fields,
     * while others consume the integer pixel PointDelta fields populated by
     * CGEventCreateScrollWheelEvent(). Preserve sub-pixel core output in the
     * fixed-point representation and carry fractional pixels across ticks for
     * the PointDelta representation instead of rounding every 2 ms tick away.
     */
    point_vertical = take_point_delta(vertical, &app->point_remainder_y);
    point_horizontal = take_point_delta(horizontal, &app->point_remainder_x);

    event = CGEventCreateScrollWheelEvent(NULL, kCGScrollEventUnitPixel,
                                          2, point_vertical, point_horizontal);
    if (!event)
        return;

    CGEventSetIntegerValueField(event, kCGScrollWheelEventIsContinuous, 1);

    /* Set the precise forms after construction; setting DeltaAxis can cause
     * CoreGraphics to recalculate PointDelta/FixedPtDelta internally. */
    CGEventSetDoubleValueField(event, kCGScrollWheelEventFixedPtDeltaAxis1,
                               vertical);
    CGEventSetDoubleValueField(event, kCGScrollWheelEventFixedPtDeltaAxis2,
                               horizontal);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventPointDeltaAxis1,
                                point_vertical);
    CGEventSetIntegerValueField(event, kCGScrollWheelEventPointDeltaAxis2,
                                point_horizontal);

    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
}

static void
post_button(CGEventType type, CGMouseButton button)
{
    CGEventRef current = CGEventCreate(NULL);
    CGEventRef event;
    CGPoint position;

    if (!current)
        return;
    position = CGEventGetLocation(current);
    CFRelease(current);

    event = CGEventCreateMouseEvent(NULL, type, position, button);
    if (!event)
        return;
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
}

static void
post_relative_motion(int64_t dx, int64_t dy, const struct app *app)
{
    CGEventRef current = CGEventCreate(NULL);
    CGEventRef event;
    CGPoint position;
    CGEventType type = kCGEventMouseMoved;
    CGMouseButton button = kCGMouseButtonLeft;

    if (!current)
        return;
    position = CGEventGetLocation(current);
    CFRelease(current);

    position.x += (double)dx;
    position.y += (double)dy;

    if (app->left_down) {
        type = kCGEventLeftMouseDragged;
        button = kCGMouseButtonLeft;
    } else if (app->right_down) {
        type = kCGEventRightMouseDragged;
        button = kCGMouseButtonRight;
    }

    event = CGEventCreateMouseEvent(NULL, type, position, button);
    if (!event)
        return;
    CGEventSetIntegerValueField(event, kCGMouseEventDeltaX, dx);
    CGEventSetIntegerValueField(event, kCGMouseEventDeltaY, dy);
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
}

static void
middle_transition(struct app *app, bool down, uint64_t time_us)
{
    int status;

    if (down == app->middle_down)
        return;

    app->middle_down = down;
    app->point_remainder_x = 0.0;
    app->point_remainder_y = 0.0;
    if (down)
        status = tpsc_engine_begin(app->engine, time_us);
    else
        status = tpsc_engine_end(app->engine, time_us);

    if (status != TPSC_OK)
        fprintf(stderr, "trackpoint: core gesture transition failed: %d\n", status);
    else if (app->verbose)
        fprintf(stderr, "trackpoint: middle %s at %" PRIu64 " us\n",
                down ? "down" : "up", time_us);
}

static void
handle_button(struct app *app, uint32_t usage, bool down, uint64_t time_us)
{
    if (usage == MIDDLE_BUTTON_USAGE) {
        middle_transition(app, down, time_us);
        if (app->seize && !app->suppress_middle_click)
            post_button(down ? kCGEventOtherMouseDown : kCGEventOtherMouseUp,
                        kCGMouseButtonCenter);
        return;
    }

    if (!app->seize)
        return;

    if (usage == 1) {
        app->left_down = down;
        post_button(down ? kCGEventLeftMouseDown : kCGEventLeftMouseUp,
                    kCGMouseButtonLeft);
    } else if (usage == 2) {
        app->right_down = down;
        post_button(down ? kCGEventRightMouseDown : kCGEventRightMouseUp,
                    kCGMouseButtonRight);
    }
}

static void
handle_motion(struct app *app, uint32_t usage, int64_t value, uint64_t time_us)
{
    struct tpsc_vec delta = {0.0, 0.0};
    int status;

    if (value == 0)
        return;

    if (!app->middle_down) {
        if (app->seize) {
            if (usage == kHIDUsage_GD_X)
                post_relative_motion(value, 0, app);
            else if (usage == kHIDUsage_GD_Y)
                post_relative_motion(0, value, app);
        }
        return;
    }

    if (usage == kHIDUsage_GD_X)
        delta.x = (double)value;
    else if (usage == kHIDUsage_GD_Y)
        delta.y = (double)value;
    else
        return;

    status = tpsc_engine_feed(app->engine, time_us, delta);
    if (status != TPSC_OK)
        fprintf(stderr, "trackpoint: core feed failed: %d\n", status);
    else if (app->verbose)
        fprintf(stderr, "trackpoint: raw %c=%" PRId64 " at %" PRIu64 " us\n",
                usage == kHIDUsage_GD_X ? 'x' : 'y', value, time_us);
}

static void
input_value(void *context, IOReturn result, void *sender, IOHIDValueRef value)
{
    struct app *app = context;
    IOHIDElementRef element;
    uint32_t page;
    uint32_t usage;
    int64_t integer_value;
    uint64_t time_us;

    (void)result;
    (void)sender;

    element = IOHIDValueGetElement(value);
    if (!element)
        return;

    page = IOHIDElementGetUsagePage(element);
    usage = IOHIDElementGetUsage(element);
    integer_value = IOHIDValueGetIntegerValue(value);
    time_us = mach_ticks_to_us(IOHIDValueGetTimeStamp(value));

    if (page == kHIDPage_GenericDesktop &&
        (usage == kHIDUsage_GD_X || usage == kHIDUsage_GD_Y)) {
        handle_motion(app, usage, integer_value, time_us);
    } else if (page == kHIDPage_Button) {
        handle_button(app, usage, integer_value != 0, time_us);
    }
}

static void
tick_callback(CFRunLoopTimerRef timer, void *context)
{
    struct app *app = context;
    struct tpsc_vec output;
    uint64_t time_us;
    int status;

    (void)timer;

    if (!app->middle_down || !tpsc_engine_needs_ticks(app->engine))
        return;

    time_us = now_us();
    status = tpsc_engine_tick(app->engine, time_us, &output);
    if (status != TPSC_OK) {
        fprintf(stderr, "trackpoint: core tick failed: %d\n", status);
        return;
    }

    if (output.x != 0.0 || output.y != 0.0) {
        double horizontal = app->x_sign * output.x * app->scroll_scale;
        double vertical = app->y_sign * output.y * app->scroll_scale;
        post_scroll(app, vertical, horizontal);
        if (app->verbose)
            fprintf(stderr, "trackpoint: scroll x=%g y=%g\n",
                    horizontal, vertical);
    }
}

static CGEventRef
event_tap_callback(CGEventTapProxy proxy, CGEventType type,
                   CGEventRef event, void *context)
{
    struct app *app = context;
    int64_t button;

    (void)proxy;

    if (type == kCGEventTapDisabledByTimeout ||
        type == kCGEventTapDisabledByUserInput) {
        if (app->event_tap)
            CGEventTapEnable(app->event_tap, true);
        return event;
    }

    if (!app->target_present || app->seize)
        return event;

    if (type == kCGEventOtherMouseDown || type == kCGEventOtherMouseUp) {
        button = CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber);
        if (button == kCGMouseButtonCenter && app->suppress_middle_click)
            return NULL;
    }

    if (app->middle_down &&
        (type == kCGEventMouseMoved ||
         type == kCGEventLeftMouseDragged ||
         type == kCGEventRightMouseDragged ||
         type == kCGEventOtherMouseDragged))
        return NULL;

    return event;
}

static int
setup_event_tap(struct app *app)
{
    CGEventMask mask;

    if (app->seize)
        return 0;

    mask = CGEventMaskBit(kCGEventMouseMoved) |
           CGEventMaskBit(kCGEventLeftMouseDragged) |
           CGEventMaskBit(kCGEventRightMouseDragged) |
           CGEventMaskBit(kCGEventOtherMouseDragged) |
           CGEventMaskBit(kCGEventOtherMouseDown) |
           CGEventMaskBit(kCGEventOtherMouseUp);

    app->event_tap = CGEventTapCreate(kCGHIDEventTap,
                                      kCGHeadInsertEventTap,
                                      kCGEventTapOptionDefault,
                                      mask,
                                      event_tap_callback,
                                      app);
    if (!app->event_tap) {
        fprintf(stderr,
                "trackpoint: cannot create Quartz event tap. Grant Input "
                "Monitoring/Accessibility permission and relaunch.\n");
        return -1;
    }

    app->event_tap_source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault,
                                                          app->event_tap, 0);
    if (!app->event_tap_source)
        return -1;

    CFRunLoopAddSource(CFRunLoopGetMain(), app->event_tap_source,
                       kCFRunLoopCommonModes);
    CGEventTapEnable(app->event_tap, true);
    return 0;
}

static int
setup_tick_timer(struct app *app)
{
    CFRunLoopTimerContext context = {0, app, NULL, NULL, NULL};
    double interval = (double)tpsc_engine_tick_us(app->engine) / 1000000.0;

    app->tick_timer = CFRunLoopTimerCreate(kCFAllocatorDefault,
                                           CFAbsoluteTimeGetCurrent() + interval,
                                           interval, 0, 0,
                                           tick_callback, &context);
    if (!app->tick_timer)
        return -1;

    CFRunLoopAddTimer(CFRunLoopGetMain(), app->tick_timer,
                      kCFRunLoopCommonModes);
    return 0;
}

static int
setup_hid(struct app *app)
{
    CFMutableDictionaryRef match;
    IOReturn status;
    IOOptionBits options = app->seize ? kIOHIDOptionsTypeSeizeDevice
                                      : kIOHIDOptionsTypeNone;

    app->hid_manager = IOHIDManagerCreate(kCFAllocatorDefault,
                                           kIOHIDOptionsTypeNone);
    if (!app->hid_manager)
        return -1;

    match = matching_dictionary(app);
    if (!match)
        return -1;

    IOHIDManagerSetDeviceMatching(app->hid_manager, match);
    CFRelease(match);

    IOHIDManagerRegisterDeviceMatchingCallback(app->hid_manager,
                                                device_matched, app);
    IOHIDManagerRegisterDeviceRemovalCallback(app->hid_manager,
                                               device_removed, app);
    IOHIDManagerRegisterInputValueCallback(app->hid_manager,
                                            input_value, app);
    IOHIDManagerScheduleWithRunLoop(app->hid_manager,
                                    CFRunLoopGetMain(),
                                    kCFRunLoopCommonModes);

    status = IOHIDManagerOpen(app->hid_manager, options);
    if (status != kIOReturnSuccess) {
        fprintf(stderr, "trackpoint: IOHIDManagerOpen failed: 0x%08x%s\n",
                status,
                app->seize ? " (try running with appropriate privileges)" : "");
        return -1;
    }

    return 0;
}

static int
setup_core(struct app *app)
{
    struct tpsc_engine_config cfg;
    int status;

    tpsc_profile_defaults_hyperbolic(&app->profile);
    app->profile.clamp_negative_output = false;

    tpsc_engine_config_defaults(&cfg);
    cfg.first_step_distance = 0.4;
    cfg.first_step_axis_merge_ms = 70.0;
    cfg.first_step_max_reports = -1;
    cfg.idle_reset_ms = 333.3;
    cfg.transform.apply = tpsc_profile_transform;
    cfg.transform.userdata = &app->profile;

    app->engine = tpsc_engine_create(&cfg, &status);
    if (!app->engine) {
        fprintf(stderr, "trackpoint: cannot create core engine: %d\n", status);
        return -1;
    }
    return 0;
}

static void
cleanup(struct app *app)
{
    if (app->hid_manager) {
        IOHIDManagerUnscheduleFromRunLoop(app->hid_manager,
                                          CFRunLoopGetMain(),
                                          kCFRunLoopCommonModes);
        IOHIDManagerClose(app->hid_manager, kIOHIDOptionsTypeNone);
        CFRelease(app->hid_manager);
    }
    if (app->tick_timer) {
        CFRunLoopTimerInvalidate(app->tick_timer);
        CFRelease(app->tick_timer);
    }
    if (app->event_tap_source) {
        CFRunLoopRemoveSource(CFRunLoopGetMain(), app->event_tap_source,
                              kCFRunLoopCommonModes);
        CFRelease(app->event_tap_source);
    }
    if (app->event_tap)
        CFRelease(app->event_tap);
    tpsc_engine_destroy(app->engine);
}

int
main(int argc, char **argv)
{
    struct app app = {
        .vendor_id = DEFAULT_VENDOR_ID,
        .product_id = DEFAULT_PRODUCT_ID,
        .scroll_scale = 8.0,
        .x_sign = -1.0,
        .y_sign = -1.0,
        .suppress_middle_click = true,
    };

    (void)mach_timebase_info(&g_timebase);

    if (parse_args(&app, argc, argv) != 0) {
        usage(argv[0]);
        return 2;
    }

    if (setup_core(&app) != 0 ||
        setup_event_tap(&app) != 0 ||
        setup_tick_timer(&app) != 0 ||
        setup_hid(&app) != 0) {
        cleanup(&app);
        return 1;
    }

    fprintf(stderr,
            "trackpoint: running for vid=%04x pid=%04x, scale=%g%s\n",
            app.vendor_id, app.product_id, app.scroll_scale,
            app.seize ? " [exclusive seize mode]" : "");
    CFRunLoopRun();
    cleanup(&app);
    return 0;
}
