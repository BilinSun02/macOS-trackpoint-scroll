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
static bool g_pin_middle_down;

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

    /*
     * Keep receiving hardware mouse deltas while decoupling them from the
     * visible cursor. Unlike repeatedly warping the cursor, this does not rely
     * on Quartz's 0.25 s local-event suppression interval and therefore can be
     * undone immediately when the middle button is released.
     */
    (void)CGAssociateMouseAndMouseCursorPosition(down ? false : true);
}

static void
cursor_pin_cleanup(void)
{
    if (g_pin_middle_down) {
        (void)CGAssociateMouseAndMouseCursorPosition(true);
        g_pin_middle_down = false;
    }

    if (g_pin_hid_manager) {
        IOHIDManagerUnscheduleFromRunLoop(g_pin_hid_manager,
                                          CFRunLoopGetMain(),
                                          kCFRunLoopCommonModes);
        IOHIDManagerClose(g_pin_hid_manager, kIOHIDOptionsTypeNone);
        CFRelease(g_pin_hid_manager);
        g_pin_hid_manager = NULL;
    }
}

__attribute__((constructor)) static void
cursor_pin_init(void)
{
    CFMutableDictionaryRef match;

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

    atexit(cursor_pin_cleanup);
}
