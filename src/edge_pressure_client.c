#include "edge_pressure_client.h"
#include "edge_pressure_protocol.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static bool g_enabled;
static int g_fd = -1;
static bool g_reported_unavailable;

static void
disconnect_helper(void)
{
    if (g_fd >= 0)
        close(g_fd);
    g_fd = -1;
}

void
tpsc_edge_pressure_client_set_enabled(bool enabled)
{
    g_enabled = enabled;
    if (!enabled)
        disconnect_helper();
}

static bool
connect_helper(void)
{
    struct sockaddr_un addr;
    char path[sizeof(addr.sun_path)];
    int no_sigpipe = 1;
    int n;

    if (!g_enabled)
        return false;
    if (g_fd >= 0)
        return true;

    n = snprintf(path, sizeof(path), "%s%u.sock",
                 TPSC_EDGE_PRESSURE_SOCKET_PREFIX, (unsigned)getuid());
    if (n < 0 || (size_t)n >= sizeof(path))
        return false;

    g_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_fd < 0)
        return false;

    /*
     * A helper restart or disconnect can race with a report write. Without
     * SO_NOSIGPIPE, Darwin may terminate this process with SIGPIPE before the
     * write returns EPIPE, bypassing the reconnect-and-retry path below.
     */
    if (setsockopt(g_fd, SOL_SOCKET, SO_NOSIGPIPE,
                   &no_sigpipe, sizeof(no_sigpipe)) != 0) {
        fprintf(stderr,
                "trackpoint: cannot disable SIGPIPE on helper socket: %s\n",
                strerror(errno));
        disconnect_helper();
        return false;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, path, (size_t)n + 1);

    if (connect(g_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        if (!g_reported_unavailable) {
            fprintf(stderr,
                    "trackpoint: edge-pressure helper unavailable at %s: %s\n",
                    path, strerror(errno));
            g_reported_unavailable = true;
        }
        disconnect_helper();
        return false;
    }

    g_reported_unavailable = false;
    fprintf(stderr, "trackpoint: connected to edge-pressure helper\n");
    return true;
}

static bool
write_all(const void *buffer, size_t size)
{
    const uint8_t *p = buffer;

    while (size > 0) {
        ssize_t n = write(g_fd, p, size);

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

bool
tpsc_edge_pressure_post_report(int64_t dx, int64_t dy,
                                int64_t vertical_wheel,
                                int64_t horizontal_wheel,
                                uint32_t buttons)
{
    struct tpsc_edge_pressure_message message = {
        .dx = dx,
        .dy = dy,
        .vertical_wheel = vertical_wheel,
        .horizontal_wheel = horizontal_wheel,
        .buttons = buttons,
    };

    if (!connect_helper())
        return false;

    if (write_all(&message, sizeof(message)))
        return true;

    disconnect_helper();

    /* One reconnect attempt makes helper restarts transparent. */
    if (!connect_helper())
        return false;
    if (write_all(&message, sizeof(message)))
        return true;

    disconnect_helper();
    return false;
}

bool
tpsc_edge_pressure_post_state(int64_t dx, int64_t dy, uint32_t buttons)
{
    return tpsc_edge_pressure_post_report(dx, dy, 0, 0, buttons);
}

bool
tpsc_edge_pressure_post(int64_t dx, int64_t dy)
{
    if (dx == 0 && dy == 0)
        return true;
    return tpsc_edge_pressure_post_report(dx, dy, 0, 0, 0);
}
