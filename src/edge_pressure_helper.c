#include "edge_pressure_protocol.h"
#include "karabiner_vhid.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hidsystem/IOHIDEventSystemClient.h>

#include <dlfcn.h>
#include <errno.h>
#include <inttypes.h>
#include <mach/mach_time.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

typedef struct __IOHIDEvent *tpsc_IOHIDEventRef;
typedef double tpsc_IOHIDFloat;

typedef IOHIDEventSystemClientRef (*tpsc_hid_client_create_fn)(CFAllocatorRef);
typedef tpsc_IOHIDEventRef (*tpsc_hid_scroll_create_fn)(
    CFAllocatorRef, uint64_t, tpsc_IOHIDFloat, tpsc_IOHIDFloat,
    tpsc_IOHIDFloat, IOOptionBits);
typedef void (*tpsc_hid_event_set_flags_fn)(tpsc_IOHIDEventRef, uint32_t);
typedef void (*tpsc_hid_dispatch_fn)(IOHIDEventSystemClientRef,
                                     tpsc_IOHIDEventRef);

#define TPSC_IOHID_ACCELERATED 0x00010000u

static IOHIDEventSystemClientRef g_scroll_event_client;
static tpsc_hid_scroll_create_fn g_hid_scroll_create;
static tpsc_hid_event_set_flags_fn g_hid_event_set_flags;
static tpsc_hid_dispatch_fn g_hid_dispatch;
static bool g_scroll_spi_initialized;
static bool g_scroll_spi_available;

static bool
initialize_direct_scroll_spi(void)
{
    tpsc_hid_client_create_fn create_client;

    if (g_scroll_spi_initialized)
        return g_scroll_spi_available;

    g_scroll_spi_initialized = true;

    create_client = (tpsc_hid_client_create_fn)
        dlsym(RTLD_DEFAULT, "IOHIDEventSystemClientCreate");
    g_hid_scroll_create = (tpsc_hid_scroll_create_fn)
        dlsym(RTLD_DEFAULT, "IOHIDEventCreateScrollEvent");
    g_hid_event_set_flags = (tpsc_hid_event_set_flags_fn)
        dlsym(RTLD_DEFAULT, "IOHIDEventSetEventFlags");
    g_hid_dispatch = (tpsc_hid_dispatch_fn)
        dlsym(RTLD_DEFAULT, "IOHIDEventSystemClientDispatchEvent");

    if (!create_client || !g_hid_scroll_create ||
        !g_hid_event_set_flags || !g_hid_dispatch) {
        fprintf(stderr,
                "edge-pressure-helper: direct HID scroll SPI unavailable\n");
        return false;
    }

    g_scroll_event_client = create_client(kCFAllocatorDefault);
    if (!g_scroll_event_client) {
        fprintf(stderr,
                "edge-pressure-helper: cannot create HID event-system client "
                "for direct scrolling\n");
        return false;
    }

    g_scroll_spi_available = true;
    fprintf(stderr,
            "edge-pressure-helper: direct floating-point HID scroll ready\n");
    return true;
}

static bool
post_direct_scroll(double scroll_x, double scroll_y)
{
    tpsc_IOHIDEventRef event;

    if (scroll_x == 0.0 && scroll_y == 0.0)
        return true;
    if (!initialize_direct_scroll_spi())
        return false;

    event = g_hid_scroll_create(kCFAllocatorDefault,
                                mach_absolute_time(),
                                scroll_x, scroll_y, 0.0, 0);
    if (!event)
        return false;

    /*
     * Apple's IOHIDPointerScrollFilter only applies its acceleration curve
     * when kIOHIDAccelerated is absent. These values are already the final
     * linear output of trackpoint-scroll-core, so mark them as processed.
     */
    g_hid_event_set_flags(event, TPSC_IOHID_ACCELERATED);
    g_hid_dispatch(g_scroll_event_client, event);
    CFRelease(event);
    return true;
}

static volatile sig_atomic_t g_exit_requested;
static int g_listen_fd = -1;
static char g_socket_path[104];

static void
handle_signal(int signo)
{
    (void)signo;
    g_exit_requested = 1;
    if (g_listen_fd >= 0)
        close(g_listen_fd);
}

static bool
read_all(int fd, void *buffer, size_t size)
{
    uint8_t *p = buffer;

    while (size > 0) {
        ssize_t n = read(fd, p, size);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (n == 0)
            return false;

        p += (size_t)n;
        size -= (size_t)n;
    }

    return true;
}

static int
serve_client(int fd, uid_t allowed_uid)
{
    uid_t peer_uid;
    gid_t peer_gid;

    if (getpeereid(fd, &peer_uid, &peer_gid) != 0) {
        fprintf(stderr, "edge-pressure-helper: getpeereid failed: %s\n",
                strerror(errno));
        return -1;
    }

    if (peer_uid != allowed_uid) {
        fprintf(stderr,
                "edge-pressure-helper: rejected uid %u (expected %u)\n",
                (unsigned)peer_uid, (unsigned)allowed_uid);
        return -1;
    }

    fprintf(stderr, "edge-pressure-helper: client uid %u connected\n",
            (unsigned)peer_uid);

    while (!g_exit_requested) {
        struct pollfd pfd = {
            .fd = fd,
            .events = POLLIN,
        };
        struct tpsc_edge_pressure_message message;
        int pr;

        do {
            /*
             * Karabiner's Unix-domain transport expects regular heartbeats.
             * Keep its virtual-HID peer alive even when the TrackPoint is idle.
             */
            pr = poll(&pfd, 1, 2500);
        } while (pr < 0 && errno == EINTR);

        if (pr == 0) {
            if (!tpsc_vhid_keepalive()) {
                fprintf(stderr,
                        "edge-pressure-helper: virtual HID keepalive failed\n");
            }
            continue;
        }

        if (pr < 0)
            return -1;

        if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
            break;
        if ((pfd.revents & POLLIN) == 0)
            continue;

        if (!read_all(fd, &message, sizeof(message)))
            break;

        if ((message.dx != 0 || message.dy != 0 ||
             message.buttons != 0) &&
            !tpsc_vhid_post_pointing(message.buttons,
                                     message.dx, message.dy)) {
            fprintf(stderr,
                    "edge-pressure-helper: virtual HID forwarding failed\n");
            return -1;
        }

        if ((message.scroll_x != 0.0 || message.scroll_y != 0.0) &&
            !post_direct_scroll(message.scroll_x, message.scroll_y)) {
            fprintf(stderr,
                    "edge-pressure-helper: direct HID scroll forwarding failed\n");
            return -1;
        }
    }

    /*
     * Never let a user-daemon disconnect leave virtual mouse buttons held.
     * A zero-motion all-buttons-up report is valid on Karabiner's pointing
     * device and resets the virtual device's button state.
     */
    (void)tpsc_vhid_post_pointing(0, 0, 0);
    return 0;
}

static int
parse_uid(const char *text, uid_t *uid)
{
    char *end = NULL;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno || !end || *end != '\0' || value > UINT32_MAX)
        return -1;

    *uid = (uid_t)value;
    return 0;
}

int
main(int argc, char **argv)
{
    struct sockaddr_un addr;
    uid_t allowed_uid;
    int n;

    if (argc != 3 || strcmp(argv[1], "--uid") != 0 ||
        parse_uid(argv[2], &allowed_uid) != 0) {
        fprintf(stderr, "usage: %s --uid UID\n", argv[0]);
        return 2;
    }

    if (geteuid() != 0) {
        fprintf(stderr, "edge-pressure-helper: must run as root\n");
        return 1;
    }

    if (tpsc_vhid_initialize() != 0)
        return 1;

    n = snprintf(g_socket_path, sizeof(g_socket_path), "%s%u.sock",
                 TPSC_EDGE_PRESSURE_SOCKET_PREFIX, (unsigned)allowed_uid);
    if (n < 0 || (size_t)n >= sizeof(g_socket_path)) {
        fprintf(stderr, "edge-pressure-helper: socket path too long\n");
        tpsc_vhid_shutdown();
        return 1;
    }

    unlink(g_socket_path);

    g_listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_listen_fd < 0) {
        perror("edge-pressure-helper: socket");
        tpsc_vhid_shutdown();
        return 1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, g_socket_path, (size_t)n + 1);

    if (bind(g_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "edge-pressure-helper: bind %s: %s\n",
                g_socket_path, strerror(errno));
        close(g_listen_fd);
        tpsc_vhid_shutdown();
        return 1;
    }

    if (chown(g_socket_path, allowed_uid, (gid_t)-1) != 0 ||
        chmod(g_socket_path, S_IRUSR | S_IWUSR) != 0) {
        fprintf(stderr,
                "edge-pressure-helper: cannot secure socket %s: %s\n",
                g_socket_path, strerror(errno));
        close(g_listen_fd);
        unlink(g_socket_path);
        tpsc_vhid_shutdown();
        return 1;
    }

    if (listen(g_listen_fd, 4) != 0) {
        perror("edge-pressure-helper: listen");
        close(g_listen_fd);
        unlink(g_socket_path);
        tpsc_vhid_shutdown();
        return 1;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    fprintf(stderr,
            "edge-pressure-helper: ready for uid %u at %s\n",
            (unsigned)allowed_uid, g_socket_path);

    while (!g_exit_requested) {
        struct pollfd pfd = {
            .fd = g_listen_fd,
            .events = POLLIN,
        };
        int pr;
        int fd;

        do {
            pr = poll(&pfd, 1, 2500);
        } while (pr < 0 && errno == EINTR);

        if (pr == 0) {
            /*
             * The helper may sit here before the user daemon first reaches an
             * edge. Keep Karabiner's root-only virtual-HID connection alive
             * even when no user client has connected yet.
             */
            if (!tpsc_vhid_keepalive()) {
                fprintf(stderr,
                        "edge-pressure-helper: virtual HID keepalive failed\n");
            }
            continue;
        }

        if (pr < 0) {
            if (g_exit_requested)
                break;
            fprintf(stderr, "edge-pressure-helper: listen poll: %s\n",
                    strerror(errno));
            continue;
        }

        if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
            break;
        if ((pfd.revents & POLLIN) == 0)
            continue;

        fd = accept(g_listen_fd, NULL, NULL);
        if (fd < 0) {
            if (errno == EINTR)
                continue;
            if (g_exit_requested)
                break;
            fprintf(stderr, "edge-pressure-helper: accept: %s\n",
                    strerror(errno));
            continue;
        }

        (void)serve_client(fd, allowed_uid);
        close(fd);
    }

    if (g_listen_fd >= 0)
        close(g_listen_fd);
    unlink(g_socket_path);
    tpsc_vhid_shutdown();
    if (g_scroll_event_client) {
        CFRelease(g_scroll_event_client);
        g_scroll_event_client = NULL;
    }
    return 0;
}
