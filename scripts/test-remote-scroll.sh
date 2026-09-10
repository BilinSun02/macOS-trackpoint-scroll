#!/bin/sh
set -eu

if [ "$#" -lt 1 ]; then
    echo "usage: $0 user@host" >&2
    exit 2
fi

REMOTE="$1"

ssh -t "$REMOTE" '
set -eu
LOG="$HOME/Library/Logs/macOS-trackpoint-scroll/stderr.log"

# Authenticate up front so the password prompt does not appear after the timed
# interaction has already finished.
sudo -v

echo
echo "=== middle-scroll test ==="
echo "For the next 8 seconds, hold the TrackPoint middle button and move the"
echo "TrackPoint enough that scrolling should clearly occur."
: > "$LOG"
sleep 8

echo
echo "=== middle transitions ==="
grep -E "trackpoint: middle (down|up)" "$LOG" || echo "(none)"
echo
echo "=== raw motion while middle held ==="
grep -E "trackpoint: raw [xy]=" "$LOG" | head -80 || echo "(none)"
echo
echo "=== scroll outputs ==="
grep -E "trackpoint: scroll x=" "$LOG" | head -80 || echo "(none)"
echo
echo "=== core / posting errors ==="
grep -E "core (gesture transition|feed|tick) failed|virtual-HID .* failed|IOHIDManagerOpen failed" "$LOG" || echo "(none)"
echo
echo "=== daemon startup identity ==="
grep -E "privacy |matched HID|running for|raw HID pointer curve" "$LOG" || true
echo
echo "=== direct HID helper scroll path ==="
sudo grep -E "direct floating-point HID scroll ready|direct HID scroll SPI unavailable|cannot create HID event-system client|direct HID scroll forwarding failed" \
  "/Library/Logs/macOS-trackpoint-scroll/edge-pressure-helper.stderr.log" | tail -20 || true
'
