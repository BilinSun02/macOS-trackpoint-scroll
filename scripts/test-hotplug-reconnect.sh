#!/bin/sh
set -eu

LABEL="io.github.bilinsun02.macos-trackpoint-scroll"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
USER_LOG="$HOME/Library/Logs/macOS-trackpoint-scroll/stderr.log"
HELPER_LOG="/Library/Logs/macOS-trackpoint-scroll/edge-pressure-helper.stderr.log"
MODE="${1:---capture-only}"
BACKUP=""
VERBOSE_ADDED=0

usage() {
    cat <<'EOF'
usage: scripts/test-hotplug-reconnect.sh [--capture-only|--reproduce]

  --capture-only  Snapshot the current daemon/helper state without restarting
                  anything. This is the default and is the mode to use when
                  intermittent broken scrolling is already present.

  --reproduce     First snapshot the current state, then restart once with
                  focused verbose logging and run an unplug/replug experiment.
EOF
}

print_snapshot() {
    echo
    echo "=== user-daemon lifecycle / errors ==="
    grep -E \
      'privacy |active scroll rewrite|VHID scroll carrier|matched HID|target HID device removed|middle (down|up)|core .*failed|virtual-HID .*failed|edge-pressure helper' \
      "$USER_LOG" 2>/dev/null | tail -n 200 || true

    echo
    echo "=== recent verbose motion / scroll (if enabled) ==="
    grep -E \
      'middle (down|up)|raw [xy]=|scroll x=|core .*failed|virtual-HID .*failed' \
      "$USER_LOG" 2>/dev/null | tail -n 120 || true

    echo
    echo "=== root virtual-HID helper log ==="
    sudo tail -n 200 "$HELPER_LOG" 2>/dev/null || true

    echo
    echo "=== LaunchAgent state ==="
    launchctl print "gui/$(id -u)/$LABEL" 2>&1 |
      grep -E 'state =|pid =|last exit code|program =|arguments =|--seize|--edge-pressure-helper|--verbose' || true
}

cleanup() {
    exit_status=$?

    if [ "$VERBOSE_ADDED" -eq 1 ] && [ -n "$BACKUP" ] && [ -f "$BACKUP" ]; then
        cp "$BACKUP" "$PLIST"
        launchctl bootout "gui/$(id -u)" "$PLIST" >/dev/null 2>&1 || true
        launchctl bootstrap "gui/$(id -u)" "$PLIST" >/dev/null 2>&1 || true
        launchctl kickstart -k "gui/$(id -u)/$LABEL" >/dev/null 2>&1 || true
    fi
    if [ -n "$BACKUP" ]; then
        rm -f "$BACKUP"
    fi
    exit "$exit_status"
}
trap cleanup EXIT HUP INT TERM

case "$MODE" in
    --capture-only)
        ;;
    --reproduce)
        ;;
    -h|--help)
        usage
        exit 0
        ;;
    *)
        usage >&2
        exit 2
        ;;
esac

if [ ! -f "$PLIST" ]; then
    echo "error: LaunchAgent not installed: $PLIST" >&2
    exit 1
fi

# Authenticate before collecting the root helper log. This does not alter or
# restart either process, so an already-broken state remains intact.
sudo -v

if [ "$MODE" = "--capture-only" ]; then
    cat <<'EOF'
=== hot-plug scrolling state capture ===
No process will be restarted or reconfigured.
EOF
    print_snapshot
    cat <<'EOF'

If scrolling is currently broken, preserve this output before kickstarting the
agent. The normal (non-verbose) daemon always logs device match/removal and
transport/lifecycle errors; a prior --reproduce run may additionally provide
raw/middle/scroll diagnostics.
EOF
    exit 0
fi

cat <<'EOF'
=== pre-restart state snapshot ===
This is captured before the diagnostic changes or restarts anything.
EOF
print_snapshot

BACKUP="$(mktemp -t macos-trackpoint-scroll-plist.XXXXXX)"
cp "$PLIST" "$BACKUP"

if ! /usr/libexec/PlistBuddy -c 'Print :ProgramArguments' "$PLIST" 2>/dev/null |
    grep -qx '    --verbose'; then
    /usr/libexec/PlistBuddy -c 'Add :ProgramArguments:3 string --verbose' "$PLIST"
    VERBOSE_ADDED=1
fi

# The explicit reproduction mode intentionally starts with fresh logs and one
# reload. Never use this mode as the first action after observing the rare
# failure; --capture-only is designed for that case.
: > "$USER_LOG"
sudo sh -c ": > '$HELPER_LOG'"
launchctl bootout "gui/$(id -u)" "$PLIST" >/dev/null 2>&1 || true
launchctl bootstrap "gui/$(id -u)" "$PLIST"
launchctl kickstart -k "gui/$(id -u)/$LABEL"
sleep 2

cat <<'EOF'

=== hot-plug scrolling reproduction ===
The daemon is now running with focused verbose logging.

After you press Enter:
  1. Unplug the TrackPoint keyboard.
  2. Wait at least 3 seconds.
  3. Plug it back in and wait until normal pointer movement works.
  4. Hold the middle button and move the TrackPoint enough that scrolling
     should clearly occur.
  5. If scrolling is broken, DO NOT kickstart the agent.
  6. Return to this Terminal and press Enter again.

Press Enter to begin:
EOF
read _start </dev/tty

printf '%s\n' "Test now. Press Enter here only after you have tested scrolling:"
read _finish </dev/tty

print_snapshot

echo
echo "=== interpretation ==="
cat <<'EOF'
- No 'matched HID device' after replug: HID re-enumeration/matching failed.
- 'middle down' missing after a confirmed match: the middle-button transition
  did not reach the daemon.
- 'middle down' appears but no 'raw x/y': motion is not reaching the scroll
  engine while the middle button is held.
- raw x/y appears but no 'scroll x/y': the core/tick lifecycle is stale.
- scroll x/y appears with no virtual-HID forwarding error, but nothing scrolls:
  the carrier/rewrite path is the leading suspect.
EOF

if [ "$VERBOSE_ADDED" -eq 1 ]; then
    echo
    echo "Restoring the normal non-verbose LaunchAgent..."
fi
