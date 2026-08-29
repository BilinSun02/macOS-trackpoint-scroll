#include <ApplicationServices/ApplicationServices.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*
 * These symbols are exported by IOKit on macOS 10.15+, but their declarations
 * have moved between SDK header layouts. Use the stable ABI directly so this
 * development tool builds across installed Xcode SDK variants.
 */
extern uint32_t IOHIDCheckAccess(uint32_t request_type);
extern bool IOHIDRequestAccess(uint32_t request_type);

#define TPSC_IOHID_REQUEST_LISTEN_EVENT 1u
#define TPSC_IOHID_ACCESS_GRANTED       0u
#define TPSC_IOHID_ACCESS_DENIED        1u
#define TPSC_IOHID_ACCESS_UNKNOWN       2u

int tpsc_real_main(int argc, char **argv);

static const char *
access_name(uint32_t access)
{
    switch (access) {
    case TPSC_IOHID_ACCESS_GRANTED:
        return "granted";
    case TPSC_IOHID_ACCESS_DENIED:
        return "denied";
    case TPSC_IOHID_ACCESS_UNKNOWN:
        return "unknown";
    default:
        return "unexpected";
    }
}

static int
request_input_monitoring(void)
{
    uint32_t access;
    unsigned int i;

    access = IOHIDCheckAccess(TPSC_IOHID_REQUEST_LISTEN_EVENT);
    fprintf(stderr, "trackpoint: IOHID Input Monitoring access=%s (%u)\n",
            access_name(access), access);

    if (access == TPSC_IOHID_ACCESS_GRANTED)
        return 0;

    if (access == TPSC_IOHID_ACCESS_DENIED) {
        fprintf(stderr,
                "trackpoint: Input Monitoring is denied. Remove/re-enable "
                "macOS-trackpoint-scroll in Privacy & Security > Input Monitoring.\n");
        return 1;
    }

    fprintf(stderr, "trackpoint: requesting IOHID Input Monitoring access\n");
    (void)IOHIDRequestAccess(TPSC_IOHID_REQUEST_LISTEN_EVENT);

    /* Keep the requesting app alive while macOS presents/services the dialog. */
    for (i = 0; i < 120; i++) {
        usleep(500000);
        access = IOHIDCheckAccess(TPSC_IOHID_REQUEST_LISTEN_EVENT);
        if (access != TPSC_IOHID_ACCESS_UNKNOWN)
            break;
    }

    fprintf(stderr, "trackpoint: IOHID Input Monitoring final access=%s (%u)\n",
            access_name(access), access);
    return access == TPSC_IOHID_ACCESS_GRANTED ? 0 : 1;
}

static int
request_event_posting(void)
{
    bool access;

    access = CGPreflightPostEventAccess();
    fprintf(stderr, "trackpoint: Quartz event-posting access=%s\n",
            access ? "granted" : "denied");
    if (access)
        return 0;

    fprintf(stderr,
            "trackpoint: requesting Quartz event-posting access "
            "(Privacy & Security > Accessibility)\n");
    access = CGRequestPostEventAccess();
    fprintf(stderr, "trackpoint: Quartz event-posting final access=%s\n",
            access ? "granted" : "denied");
    return access ? 0 : 1;
}

static int
request_permissions(void)
{
    int hid_status = request_input_monitoring();
    int post_status = request_event_posting();
    return hid_status == 0 && post_status == 0 ? 0 : 1;
}

static void
report_permissions(void)
{
    uint32_t hid = IOHIDCheckAccess(TPSC_IOHID_REQUEST_LISTEN_EVENT);
    fprintf(stderr, "trackpoint: IOHID Input Monitoring access=%s (%u)\n",
            access_name(hid), hid);
    fprintf(stderr, "trackpoint: Quartz event-posting access=%s\n",
            CGPreflightPostEventAccess() ? "granted" : "denied");
}

int
main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--request-input-monitoring") == 0)
        return request_permissions();

    report_permissions();
    return tpsc_real_main(argc, argv);
}
