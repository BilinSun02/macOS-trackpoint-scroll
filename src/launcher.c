#include <CoreGraphics/CoreGraphics.h>

#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static volatile sig_atomic_t g_child_pid;

static void
forward_signal(int sig)
{
    pid_t pid = (pid_t)g_child_pid;
    if (pid > 0)
        kill(pid, sig);
}

static int
request_permission_only(void)
{
    if (CGPreflightListenEventAccess())
        return 0;
    return CGRequestListenEventAccess() ? 0 : 1;
}

int
main(int argc, char **argv)
{
    const char *home = getenv("HOME");
    char worker[4096];
    char config[4096];
    char *child_argv[7];
    pid_t child;
    int status;
    struct sigaction sa;

    if (argc == 2 && strcmp(argv[1], "--request-input-monitoring") == 0)
        return request_permission_only();

    if (!home || !*home) {
        fprintf(stderr, "trackpoint-launcher: HOME is not set\n");
        return 1;
    }

    if (snprintf(worker, sizeof(worker),
                 "%s/Library/Application Support/macOS-trackpoint-scroll/macOS-trackpoint-scroll",
                 home) >= (int)sizeof(worker) ||
        snprintf(config, sizeof(config),
                 "%s/.config/macOS-trackpoint-scroll.conf", home) >= (int)sizeof(config)) {
        fprintf(stderr, "trackpoint-launcher: path too long\n");
        return 1;
    }

    if (!CGPreflightListenEventAccess()) {
        fprintf(stderr,
                "trackpoint-launcher: Input Monitoring is not authorized; "
                "run the installer again to request it\n");
        return 1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = forward_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    child_argv[0] = worker;
    child_argv[1] = "--seize";
    child_argv[2] = "--verbose";
    child_argv[3] = "--config";
    child_argv[4] = config;
    child_argv[5] = NULL;

    status = posix_spawn(&child, worker, NULL, NULL, child_argv, environ);
    if (status != 0) {
        fprintf(stderr, "trackpoint-launcher: posix_spawn failed: %s\n",
                strerror(status));
        return 1;
    }
    g_child_pid = (sig_atomic_t)child;

    for (;;) {
        pid_t result = waitpid(child, &status, 0);
        if (result == child)
            break;
        if (result < 0 && errno == EINTR)
            continue;
        if (result < 0) {
            fprintf(stderr, "trackpoint-launcher: waitpid failed: %s\n",
                    strerror(errno));
            return 1;
        }
    }

    g_child_pid = 0;
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return 1;
}
