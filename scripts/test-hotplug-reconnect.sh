#!/bin/sh
set -eu

LABEL="io.github.bilinsun02.macos-trackpoint-scroll"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
USER_LOG="$HOME/Library/Logs/macOS-trackpoint-scroll/stderr.log"
HELPER_LOG="/Library/Logs/macOS-trackpoint-scroll/edge-pressure-helper.stderr.log"
BACKUP="$(mktemp -t macos-trackpoint-scroll-plist.XXXXXX)"
VERBOSE_ADDED=0

cleanup() {
    status=$?

    if [ "$VERBOSE_ADDED" -eq 1 ] && [ -f "$BACKUP" ]; then
        cp "$BACKUP" "$PLIST"
        launchctl bootout "gui/$(id -u)" "$PLIST" >/dev/null 2>&1 || true
        launchctl bootstrap "gui/$(id -u)" "$PLIST" >/dev/null 2>&1 || true
        launchctl kickstart -k "gui/$(id -u)/$LABEL" >/dev/null 2>&1 || true
    fi
    rm -f "$BACKUP"
    exit "$status"
}
trap cleanup EXIT HUP INT TERM

if [ ! -f "$PLIST" ]; then
    echo "error: LaunchAgent not installed: $PLIST" >&2
    exit 1
fi

cp "$PLIST" "$BACKUP"

# Authenticate before the interactive test so sudo cannot consume or delay the
# interesting unplug/replug interval.
sudo -v

if ! /usr/libexec/PlistBuddy -c 'Print :ProgramArguments' "$PLIST" 2>/dev/null |
    grep -qx '    --verbose'; then
    /usr/libexec/PlistBuddy -c 'Add :ProgramArguments:3 string --verbose' "$PLIST"
    VERBOSE_ADDED=1
fi

# Start from fresh logs and reload once, before the hot-plug experiment.
: > "$USER_LOG"
sudo sh -c ": > '$HELPER_LOG'"
launchctl bootout "gui/$(id -u)" "$PLIST" >/dev/null 2>&1 || true
launchctl bootstrap "gui/$(id -u)" "$PLIST"
launchctl kickstart -k "gui/$(id -u)/$LABEL"
sleep 2

cat <<'EOF'

=== hot-plug scrolling test ===
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

echo
echo "=== user-daemon lifecycle / scroll log ==="
grep -E \
  'privacy |active scroll rewrite|VHID scroll carrier|matched HID|target HID device removed|middle (down|up)|raw [xy]=|scroll x=|core .*failed|virtual-HID .*failed|edge-pressure helper' \
  "$USER_LOG" | tail -n 400 || true

echo
echo "=== root virtual-HID helper log ==="
sudo tail -n 200 "$HELPER_LOG" 2>/dev/null || true

echo
echo "=== LaunchAgent state ==="
launchctl print "gui/$(id -u)/$LABEL" 2>&1 |
  grep -E 'state =|pid =|last exit code|program =|arguments =|--seize|--edge-pressure-helper|--verbose' || true

echo
echo "=== interpretation ==="
cat <<'EOF'
- No 'matched HID device' after replug: HID re-enumeration/matching failed.
- 'middle down' missing: the replugged device is present but the middle-button
  transition did not reach the daemon.
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
