#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <IOKit/hidsystem/IOHIDLib.h>
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

#include "config.h"
#include "edge_pressure_client.h"
#include "pointer_rebound.h"
#include "trackpoint_scroll/engine.h"
#include "trackpoint_scroll/profiles.h"

#define DEFAULT_VENDOR_ID  0x5859
#define DEFAULT_PRODUCT_ID 0x0001
#define MIDDLE_BUTTON_USAGE 3
#define TPSC_VHID_SCROLL_PIXELS_PER_STEP 8.0

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
    CGPoint scroll_anchor;

    bool seize;
    bool edge_pressure_helper;
    bool vhid_pointer;
    bool verbose;
    bool suppress_middle_click;
    bool system_natural_scroll;
    bool middle_down;
    bool left_down;
    bool right_down;
    bool target_present;
    bool have_scroll_anchor;

    uint64_t pointer_diag_window_start_us;
    uint64_t pointer_diag_raw_x_events;
    uint64_t pointer_diag_raw_y_events;
    int64_t pointer_diag_raw_x_sum;
    int64_t pointer_diag_raw_y_sum;
    uint64_t pointer_diag_post_events;
    int64_t pointer_diag_post_x_sum;
    int64_t pointer_diag_post_y_sum;
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

static bool
system_natural_scroll_enabled(void)
{
    CFTypeRef value;
    bool enabled = false;

    value = CFPreferencesCopyValue(
        CFSTR("com.apple.swipescrolldirection"),
        kCFPreferencesAnyApplication,
        kCFPreferencesCurrentUser,
        kCFPreferencesAnyHost);
    if (!value)
        return false;

    if (CFGetTypeID(value) == CFBooleanGetTypeID())
        enabled = CFBooleanGetValue((CFBooleanRef)value);

    CFRelease(value);
    return enabled;
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

static const char *
hid_access_name(IOHIDAccessType access)
{
    switch (access) {
    case kIOHIDAccessTypeGranted:
        return "granted";
    case kIOHIDAccessTypeDenied:
        return "denied";
    case kIOHIDAccessTypeUnknown:
        return "unknown";
    default:
        return "unexpected";
    }
}

static void
check_privacy_access(void)
{
    IOHIDAccessType listen = IOHIDCheckAccess(kIOHIDRequestTypeListenEvent);
    IOHIDAccessType post = IOHIDCheckAccess(kIOHIDRequestTypePostEvent);

    /*
     * IOHID protected-device access is itself gated through these IOKit TCC
     * request types. Request through the same subsystem that will open the
     * TrackPoint, rather than relying on CoreGraphics to register the app.
     */
    if (listen == kIOHIDAccessTypeUnknown) {
        (void)IOHIDRequestAccess(kIOHIDRequestTypeListenEvent);
        listen = IOHIDCheckAccess(kIOHIDRequestTypeListenEvent);
    }
    if (post == kIOHIDAccessTypeUnknown) {
        (void)IOHIDRequestAccess(kIOHIDRequestTypePostEvent);
        post = IOHIDCheckAccess(kIOHIDRequestTypePostEvent);
    }

    fprintf(stderr,
            "trackpoint: privacy input-monitoring=%s accessibility=%s\n",
            hid_access_name(listen), hid_access_name(post));

    if (listen != kIOHIDAccessTypeGranted) {
        fprintf(stderr,
                "trackpoint: Input Monitoring is required to seize/read the "
                "TrackPoint; enable this app in System Settings > Privacy & "
                "Security > Input Monitoring, then restart the agent.\n");
    }

    if (post != kIOHIDAccessTypeGranted) {
        fprintf(stderr,
                "trackpoint: Accessibility is required to post replacement "
                "pointer/scroll events; enable this app in System Settings > "
                "Privacy & Security > Accessibility, then restart the agent.\n");
    }
}

static void
usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s [options]\n"
            "  --config PATH           config file (default ~/.config/macOS-trackpoint-scroll.conf)\n"
            "  --vendor HEX            HID vendor id (default 5859)\n"
            "  --product HEX           HID product id (default 0001)\n"
            "  --scroll-scale N        macOS pixel scaling (default/config 8.0)\n"
            "  --invert-x              reverse horizontal scroll direction\n"
            "  --invert-y              reverse vertical scroll direction\n"
            "  --allow-middle-click    do not suppress Quartz middle clicks\n"
            "  --seize                 exclusively claim the HID device\n"
            "  --edge-pressure-helper  use privileged virtual-HID helper at display edges\n"
            "  --vhid-pointer          route ordinary pointer motion through virtual HID\n"
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

static const char *
config_path_from_args(int argc, char **argv, char *buffer, unsigned long size)
{
    const char *path = macos_trackpoint_default_config_path(buffer, size);
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc)
            path = argv[++i];
    }
    return path;
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
        } else if (strcmp(argv[i], "--edge-pressure-helper") == 0) {
            app->edge_pressure_helper = true;
        } else if (strcmp(argv[i], "--vhid-pointer") == 0) {
            app->vhid_pointer = true;
        } else if (strcmp(argv[i], "--allow-middle-click") == 0) {
            app->suppress_middle_click = false;
        } else if (strcmp(argv[i], "--invert-x") == 0) {
            app->x_sign *= -1.0;
        } else if (strcmp(argv[i], "--invert-y") == 0) {
            app->y_sign *= -1.0;
        } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            i++;
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
    if (app->vhid_pointer &&
        (app->middle_down || app->left_down || app->right_down))
        (void)tpsc_edge_pressure_post_state(0, 0, 0);
    app->middle_down = false;
    app->left_down = false;
    app->right_down = false;
    app->have_scroll_anchor = false;
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

static uint32_t vhid_button_mask(const struct app *app);

static void
post_scroll(struct app *app, double vertical, double horizontal)
{
    CGEventRef event;
    int32_t point_vertical;
    int32_t point_horizontal;

    if (vertical == 0.0 && horizontal == 0.0)
        return;

    if (app->vhid_pointer) {
        /*
         * The scroll engine emits smooth pixel-like output. Karabiner's virtual
         * pointing device exposes signed 8-bit wheel steps instead. Preserve
         * low-speed motion by accumulating fractional wheel units across ticks
         * rather than rounding each small output independently.
         */
        /*
         * Hardware-class wheel reports are inverted again by macOS when the
         * system "Natural scrolling" preference is enabled. Compensate for
         * that here so this application's natural_scroll setting remains the
         * sole authority for the resulting direction.
         */
        {
            /*
             * HID wheel sign is opposite Quartz scroll-delta sign. If the
             * system natural-scroll setting matches this application's
             * natural_scroll setting, emit raw HID wheel direction; if the
             * settings differ, invert it. Since vertical/horizontal already
             * include the application's direction sign, this requires the
             * opposite compensation from the Quartz convention.
             */
            double system_sign = app->system_natural_scroll ? 1.0 : -1.0;

            point_vertical = take_point_delta(
                system_sign * vertical / TPSC_VHID_SCROLL_PIXELS_PER_STEP,
                &app->point_remainder_y);
            point_horizontal = take_point_delta(
                system_sign * horizontal / TPSC_VHID_SCROLL_PIXELS_PER_STEP,
                &app->point_remainder_x);
        }

        if (point_vertical == 0 && point_horizontal == 0)
            return;

        if (app->verbose)
            fprintf(stderr,
                    "trackpoint: vhid-wheel send h=%" PRId32
                    " v=%" PRId32 " from h=%g v=%g\n",
                    point_horizontal, point_vertical,
                    horizontal, vertical);

        if (!tpsc_edge_pressure_post_report(
                0, 0, point_vertical, point_horizontal,
                vhid_button_mask(app)))
            fprintf(stderr,
                    "trackpoint: virtual-HID scroll forwarding failed\n");
        return;
    }

    point_vertical = take_point_delta(vertical, &app->point_remainder_y);
    point_horizontal = take_point_delta(horizontal, &app->point_remainder_x);

    event = CGEventCreateScrollWheelEvent(NULL, kCGScrollEventUnitPixel,
                                          2, point_vertical, point_horizontal);
    if (!event)
        return;

    CGEventSetIntegerValueField(event, kCGScrollWheelEventIsContinuous, 1);
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

static uint32_t
vhid_button_mask(const struct app *app)
{
    uint32_t buttons = 0;

    if (app->left_down)
        buttons |= 1u << 0;
    if (app->right_down)
        buttons |= 1u << 1;
    if (!app->suppress_middle_click && app->middle_down)
        buttons |= 1u << 2;
    return buttons;
}

static void
post_relative_motion(int64_t dx, int64_t dy, struct app *app)
{
    if (app->vhid_pointer) {
        if (app->verbose) {
            app->pointer_diag_post_events++;
            app->pointer_diag_post_x_sum += dx;
            app->pointer_diag_post_y_sum += dy;
        }

        if (!tpsc_edge_pressure_post_state(dx, dy, vhid_button_mask(app)))
            fprintf(stderr,
                    "trackpoint: virtual-HID pointer forwarding failed\n");
        return;
    }

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

    if (app->verbose) {
        app->pointer_diag_post_events++;
        app->pointer_diag_post_x_sum += dx;
        app->pointer_diag_post_y_sum += dy;
    }

    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
}

static void
pointer_diag_observe_raw(struct app *app, uint32_t usage, int64_t value,
                         uint64_t time_us)
{
    if (!app->verbose || value == 0)
        return;

    if (app->pointer_diag_window_start_us == 0)
        app->pointer_diag_window_start_us = time_us;

    if (usage == kHIDUsage_GD_X) {
        app->pointer_diag_raw_x_events++;
        app->pointer_diag_raw_x_sum += value;
    } else if (usage == kHIDUsage_GD_Y) {
        app->pointer_diag_raw_y_events++;
        app->pointer_diag_raw_y_sum += value;
    }
}

static void
pointer_diag_maybe_report(struct app *app, uint64_t time_us)
{
    if (!app->verbose || app->pointer_diag_window_start_us == 0 ||
        time_us < app->pointer_diag_window_start_us ||
        time_us - app->pointer_diag_window_start_us < 1000000ULL)
        return;

    fprintf(stderr,
            "trackpoint: pointer-diag raw[x events=%" PRIu64 " sum=%" PRId64
            ", y events=%" PRIu64 " sum=%" PRId64
            "] post[events=%" PRIu64 " x=%" PRId64 " y=%" PRId64 "]\n",
            app->pointer_diag_raw_x_events, app->pointer_diag_raw_x_sum,
            app->pointer_diag_raw_y_events, app->pointer_diag_raw_y_sum,
            app->pointer_diag_post_events, app->pointer_diag_post_x_sum,
            app->pointer_diag_post_y_sum);

    app->pointer_diag_window_start_us = time_us;
    app->pointer_diag_raw_x_events = 0;
    app->pointer_diag_raw_y_events = 0;
    app->pointer_diag_raw_x_sum = 0;
    app->pointer_diag_raw_y_sum = 0;
    app->pointer_diag_post_events = 0;
    app->pointer_diag_post_x_sum = 0;
    app->pointer_diag_post_y_sum = 0;
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
    if (down) {
        app->have_scroll_anchor = current_cursor_position(&app->scroll_anchor);
        status = tpsc_engine_begin(app->engine, time_us);
    } else {
        app->have_scroll_anchor = false;
        status = tpsc_engine_end(app->engine, time_us);
    }

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
        if (app->seize && !app->suppress_middle_click) {
            if (app->vhid_pointer) {
                if (!tpsc_edge_pressure_post_state(
                        0, 0, vhid_button_mask(app)))
                    fprintf(stderr,
                            "trackpoint: virtual-HID button forwarding failed\n");
            } else {
                post_button(down ? kCGEventOtherMouseDown : kCGEventOtherMouseUp,
                            kCGMouseButtonCenter);
            }
        }
        return;
    }

    if (!app->seize)
        return;

    if (usage == 1) {
        app->left_down = down;
        if (app->vhid_pointer) {
            if (!tpsc_edge_pressure_post_state(0, 0,
                                                vhid_button_mask(app)))
                fprintf(stderr,
                        "trackpoint: virtual-HID button forwarding failed\n");
        } else {
            post_button(down ? kCGEventLeftMouseDown : kCGEventLeftMouseUp,
                        kCGMouseButtonLeft);
        }
    } else if (usage == 2) {
        app->right_down = down;
        if (app->vhid_pointer) {
            if (!tpsc_edge_pressure_post_state(0, 0,
                                                vhid_button_mask(app)))
                fprintf(stderr,
                        "trackpoint: virtual-HID button forwarding failed\n");
        } else {
            post_button(down ? kCGEventRightMouseDown : kCGEventRightMouseUp,
                        kCGMouseButtonRight);
        }
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
        if (app->seize && !app->middle_down)
            pointer_diag_observe_raw(app, usage, integer_value, time_us);
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

    pointer_diag_maybe_report(app, now_us());

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

static bool
is_mouse_motion_event(CGEventType type)
{
    return type == kCGEventMouseMoved ||
           type == kCGEventLeftMouseDragged ||
           type == kCGEventRightMouseDragged ||
           type == kCGEventOtherMouseDragged;
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

    if (type == kCGEventScrollWheel && app->verbose) {
        fprintf(stderr,
                "trackpoint: observed-wheel "
                "delta[h=%" PRId64 " v=%" PRId64 "] "
                "point[h=%" PRId64 " v=%" PRId64 "] "
                "fixed[h=%g v=%g] continuous=%" PRId64 "\n",
                CGEventGetIntegerValueField(
                    event, kCGScrollWheelEventDeltaAxis2),
                CGEventGetIntegerValueField(
                    event, kCGScrollWheelEventDeltaAxis1),
                CGEventGetIntegerValueField(
                    event, kCGScrollWheelEventPointDeltaAxis2),
                CGEventGetIntegerValueField(
                    event, kCGScrollWheelEventPointDeltaAxis1),
                CGEventGetDoubleValueField(
                    event, kCGScrollWheelEventFixedPtDeltaAxis2),
                CGEventGetDoubleValueField(
                    event, kCGScrollWheelEventFixedPtDeltaAxis1),
                CGEventGetIntegerValueField(
                    event, kCGScrollWheelEventIsContinuous));
        return event;
    }

    if (!app->target_present || app->seize)
        return event;

    if (type == kCGEventOtherMouseDown || type == kCGEventOtherMouseUp) {
        button = CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber);
        if (button == kCGMouseButtonCenter && app->suppress_middle_click)
            return NULL;
    }

    if (app->middle_down && is_mouse_motion_event(type)) {
        /*
         * Dropping a HID-level Quartz mouse event did not prevent WindowServer
         * from advancing the visible cursor on this adapter. Rewriting the
         * event in place avoids generating a second synthetic mouse event (and
         * therefore avoids Quartz's local-event suppression delay): applications
         * see a zero-delta event whose absolute position remains at the gesture
         * anchor.
         */
        CGEventSetIntegerValueField(event, kCGMouseEventDeltaX, 0);
        CGEventSetIntegerValueField(event, kCGMouseEventDeltaY, 0);
        if (app->have_scroll_anchor)
            CGEventSetLocation(event, app->scroll_anchor);
        return event;
    }

    return event;
}

static int
setup_event_tap(struct app *app)
{
    CGEventMask mask;
    CGEventTapOptions options = kCGEventTapOptionDefault;

    if (app->seize) {
        if (!app->verbose || !app->vhid_pointer)
            return 0;
        mask = CGEventMaskBit(kCGEventScrollWheel);
        options = kCGEventTapOptionListenOnly;
    } else {
        mask = CGEventMaskBit(kCGEventMouseMoved) |
               CGEventMaskBit(kCGEventLeftMouseDragged) |
               CGEventMaskBit(kCGEventRightMouseDragged) |
               CGEventMaskBit(kCGEventOtherMouseDragged) |
               CGEventMaskBit(kCGEventOtherMouseDown) |
               CGEventMaskBit(kCGEventOtherMouseUp);
    }

    app->event_tap = CGEventTapCreate(kCGHIDEventTap,
                                      kCGHeadInsertEventTap,
                                      options,
                                      mask,
                                      event_tap_callback,
                                      app);
    if (!app->event_tap) {
        if (app->seize) {
            fprintf(stderr,
                    "trackpoint: warning: scroll-observation event tap "
                    "unavailable; continuing without wheel telemetry\n");
            return 0;
        }
        fprintf(stderr,
                "trackpoint: cannot create Quartz event tap. Grant Input "
                "Monitoring/Accessibility permission and relaunch.\n");
        return -1;
    }

    app->event_tap_source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault,
                                                          app->event_tap, 0);
    if (!app->event_tap_source) {
        if (app->seize) {
            fprintf(stderr,
                    "trackpoint: warning: cannot create scroll-observation "
                    "run-loop source; continuing without wheel telemetry\n");
            CFRelease(app->event_tap);
            app->event_tap = NULL;
            return 0;
        }
        return -1;
    }

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
                app->seize ? " [exclusive HID open]" : "");
        if (app->seize &&
            IOHIDCheckAccess(kIOHIDRequestTypeListenEvent) !=
                kIOHIDAccessTypeGranted) {
            fprintf(stderr,
                    "trackpoint: diagnosis: Input Monitoring is not granted; "
                    "exclusive HID open cannot proceed.\n");
        } else if (app->seize) {
            fprintf(stderr,
                    "trackpoint: diagnosis: Input Monitoring reports granted, "
                    "but exclusive HID open was still denied.\n");
        }
        return -1;
    }

    return 0;
}

static const char *
profile_name(enum tpsc_profile_kind kind)
{
    switch (kind) {
    case TPSC_PROFILE_AFFINE:
        return "affine";
    case TPSC_PROFILE_QUADRATIC:
        return "quadratic";
    case TPSC_PROFILE_HYPERBOLIC:
        return "hyperbolic";
    }
    return "unknown";
}

static int
setup_core(struct app *app, const struct macos_trackpoint_config *config)
{
    struct tpsc_engine_config cfg;
    int status;

    switch (config->profile) {
    case TPSC_PROFILE_AFFINE:
        tpsc_profile_defaults_affine(&app->profile);
        app->profile.params.affine.k = config->affine_k;
        app->profile.params.affine.b = config->affine_b;
        break;
    case TPSC_PROFILE_QUADRATIC:
        tpsc_profile_defaults_quadratic(&app->profile);
        app->profile.params.quadratic.a = config->quadratic_a;
        app->profile.params.quadratic.h = config->quadratic_h;
        app->profile.params.quadratic.k = config->quadratic_k;
        break;
    case TPSC_PROFILE_HYPERBOLIC:
        tpsc_profile_defaults_hyperbolic(&app->profile);
        app->profile.params.hyperbolic.a = config->hyperbolic_a;
        app->profile.params.hyperbolic.u = config->hyperbolic_u;
        app->profile.params.hyperbolic.k = config->hyperbolic_k;
        break;
    default:
        fprintf(stderr, "trackpoint: unsupported scroll profile\n");
        return -1;
    }
    app->profile.clamp_negative_output = config->clamp_negative_output;

    tpsc_engine_config_defaults(&cfg);
    cfg.first_step_distance = config->first_step_distance;
    cfg.first_step_axis_merge_ms = config->first_step_axis_merge_ms;
    cfg.first_step_max_reports = config->first_step_max_reports;
    cfg.idle_reset_ms = config->idle_reset_ms;
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
    if (app->vhid_pointer)
        (void)tpsc_edge_pressure_post_state(0, 0, 0);
    tpsc_pointer_rebound_set_enabled(false);
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
    struct macos_trackpoint_config config;
    char config_path[1024];
    const char *selected_config;

    (void)mach_timebase_info(&g_timebase);

    macos_trackpoint_config_defaults(&config);
    selected_config = config_path_from_args(argc, argv,
                                             config_path, sizeof(config_path));
    if (selected_config &&
        macos_trackpoint_config_load(&config, selected_config, false) != 0)
        return 2;

    app.scroll_scale = config.scroll_scale;
    app.suppress_middle_click = config.suppress_middle_click;
    app.system_natural_scroll = system_natural_scroll_enabled();
    if (config.natural_scroll) {
        app.x_sign *= -1.0;
        app.y_sign *= -1.0;
    }

    if (parse_args(&app, argc, argv) != 0) {
        usage(argv[0]);
        return 2;
    }

    if ((app.edge_pressure_helper || app.vhid_pointer) && !app.seize) {
        fprintf(stderr,
                "trackpoint: --edge-pressure-helper/--vhid-pointer require --seize\n");
        return 2;
    }
    if (app.vhid_pointer && !app.edge_pressure_helper) {
        fprintf(stderr,
                "trackpoint: --vhid-pointer requires --edge-pressure-helper\n");
        return 2;
    }

    check_privacy_access();
    tpsc_edge_pressure_client_set_enabled(app.edge_pressure_helper);
    if (tpsc_pointer_rebound_set_enabled(config.rebound_filter) != 0)
        return 1;

    if (setup_core(&app, &config) != 0 ||
        setup_event_tap(&app) != 0 ||
        setup_tick_timer(&app) != 0 ||
        setup_hid(&app) != 0) {
        cleanup(&app);
        return 1;
    }

    fprintf(stderr,
            "trackpoint: system natural-scroll=%s\n",
            app.system_natural_scroll ? "enabled" : "disabled");
    fprintf(stderr,
            "trackpoint: scroll profile=%s clamp_negative_output=%s "
            "first_step_distance=%g first_step_axis_merge_ms=%g "
            "first_step_max_reports=%d idle_reset_ms=%g\n",
            profile_name(app.profile.kind),
            app.profile.clamp_negative_output ? "true" : "false",
            config.first_step_distance,
            config.first_step_axis_merge_ms,
            config.first_step_max_reports,
            config.idle_reset_ms);
    switch (app.profile.kind) {
    case TPSC_PROFILE_AFFINE:
        fprintf(stderr, "trackpoint: affine k=%g b=%g\n",
                app.profile.params.affine.k,
                app.profile.params.affine.b);
        break;
    case TPSC_PROFILE_QUADRATIC:
        fprintf(stderr, "trackpoint: quadratic a=%g h=%g k=%g\n",
                app.profile.params.quadratic.a,
                app.profile.params.quadratic.h,
                app.profile.params.quadratic.k);
        break;
    case TPSC_PROFILE_HYPERBOLIC:
        fprintf(stderr, "trackpoint: hyperbolic a=%g u=%g k=%g\n",
                app.profile.params.hyperbolic.a,
                app.profile.params.hyperbolic.u,
                app.profile.params.hyperbolic.k);
        break;
    }
    fprintf(stderr,
            "trackpoint: running for vid=%04x pid=%04x, scale=%g, direction=%s%s\n",
            app.vendor_id, app.product_id, app.scroll_scale,
            config.natural_scroll ? "natural" : "traditional",
            app.seize ? (app.vhid_pointer
                             ? " [exclusive seize + full virtual-HID pointer]"
                             : (app.edge_pressure_helper
                                    ? " [exclusive seize + virtual-HID edge pressure]"
                                    : " [exclusive seize mode]"))
                      : "");
    CFRunLoopRun();
    cleanup(&app);
    return 0;
}
