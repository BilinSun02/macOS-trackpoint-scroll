#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/hid/IOHIDUsageTables.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define CURSOR_PIN_VENDOR_ID  0x5859
#define CURSOR_PIN_PRODUCT_ID 0x0001
#define CURSOR_PIN_MIDDLE_BUTTON_USAGE 3

static IOHIDManagerRef g_pin_hid_manager;
static CFRunLoopTimerRef g_pin_timer;
static bool g_pin_middle_down;
static bool g_pin_have_anchor;
static CGPoint g_pin_anchor;

static CFMutableDictionaryRef
cursor_pin_matching_dictionary(void)
{
    CFMutableDictionaryRef dict;
    CFNumberRef vendor;
    CFNumberRef product;
    CFNumberRef usage_page;
    CFNumberRef usage;
    int v = CURSOR_PIN_VENDOR_ID;
    int p = CURSOR_PIN_PRODUCT_ID;
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

static bool
cursor_pin_get_position(CGPoint *position)
{
    CGEventRef event = CGEventCreate(NULL);

    if (!event)
        return false;
    *position = CGEventGetLocation(event);
    CFRelease(event);
    return true;
}

static void
cursor_pin_input_value(void *context, IOReturn result, void *sender,
                       IOHIDValueRef value)
{
    IOHIDElementRef element;
    uint32_t page;
    uint32_t usage;
    bool down;

    (void)context;
    (void)result;
    (void)sender;

    element = IOHIDValueGetElement(value);
    if (!element)
        return;

    page = IOHIDElementGetUsagePage(element);
    usage = IOHIDElementGetUsage(element);
    if (page != kHIDPage_Button || usage != CURSOR_PIN_MIDDLE_BUTTON_USAGE)
        return;

    down = IOHIDValueGetIntegerValue(value) != 0;
    if (down == g_pin_middle_down)
        return;

    g_pin_middle_down = down;
    if (down)
        g_pin_have_anchor = cursor_pin_get_position(&g_pin_anchor);
    else
        g_pin_have_anchor = false;
}

static void
cursor_pin_tick(CFRunLoopTimerRef timer, void *context)
{
    (void)timer;
    (void)context;

    if (g_pin_middle_down && g_pin_have_anchor)
        (void)CGWarpMouseCursorPosition(g_pin_anchor);
}

static void
cursor_pin_cleanup(void)
{
    if (g_pin_hid_manager) {
        IOHIDManagerUnscheduleFromRunLoop(g_pin_hid_manager,
                                          CFRunLoopGetMain(),
                                          kCFRunLoopCommonModes);
        IOHIDManagerClose(g_pin_hid_manager, kIOHIDOptionsTypeNone);
        CFRelease(g_pin_hid_manager);
        g_pin_hid_manager = NULL;
    }
    if (g_pin_timer) {
        CFRunLoopTimerInvalidate(g_pin_timer);
        CFRelease(g_pin_timer);
        g_pin_timer = NULL;
    }
}

__attribute__((constructor)) static void
cursor_pin_init(void)
{
    CFMutableDictionaryRef match;
    CFRunLoopTimerContext timer_context = {0, NULL, NULL, NULL, NULL};

    g_pin_hid_manager = IOHIDManagerCreate(kCFAllocatorDefault,
                                            kIOHIDOptionsTypeNone);
    if (!g_pin_hid_manager)
        return;

    match = cursor_pin_matching_dictionary();
    if (!match) {
        cursor_pin_cleanup();
        return;
    }

    IOHIDManagerSetDeviceMatching(g_pin_hid_manager, match);
    CFRelease(match);
    IOHIDManagerRegisterInputValueCallback(g_pin_hid_manager,
                                            cursor_pin_input_value, NULL);
    IOHIDManagerScheduleWithRunLoop(g_pin_hid_manager,
                                    CFRunLoopGetMain(),
                                    kCFRunLoopCommonModes);
    if (IOHIDManagerOpen(g_pin_hid_manager, kIOHIDOptionsTypeNone) !=
        kIOReturnSuccess) {
        cursor_pin_cleanup();
        return;
    }

    g_pin_timer = CFRunLoopTimerCreate(kCFAllocatorDefault,
                                       CFAbsoluteTimeGetCurrent() + 0.001,
                                       0.001, 0, 0,
                                       cursor_pin_tick, &timer_context);
    if (!g_pin_timer) {
        cursor_pin_cleanup();
        return;
    }
    CFRunLoopAddTimer(CFRunLoopGetMain(), g_pin_timer, kCFRunLoopCommonModes);
    atexit(cursor_pin_cleanup);
}
