#include <IOKit/hidsystem/IOHIDLib.h>

#include <stdbool.h>
#include <stdio.h>

__attribute__((constructor))
static void
request_hid_input_monitoring(void)
{
    IOHIDAccessType access = IOHIDCheckAccess(kIOHIDRequestTypeListenEvent);

    if (access == kIOHIDAccessTypeGranted) {
        fprintf(stderr, "trackpoint: IOHID Input Monitoring access granted\n");
        return;
    }

    fprintf(stderr,
            "trackpoint: IOHID Input Monitoring access=%d; requesting access\n",
            (int)access);

    if (!IOHIDRequestAccess(kIOHIDRequestTypeListenEvent))
        fprintf(stderr,
                "trackpoint: IOHID Input Monitoring request not granted; "
                "enable macOS-trackpoint-scroll in Privacy & Security > Input Monitoring\n");
}
