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

echo
echo "=== phase 1: below-threshold pressure ==="
echo "For the next 8 seconds, keep the cursor well away from every display edge."
echo "Press the TrackPoint gently in one direction, deliberately staying in the"
echo "range where you observe NO visible cursor movement (or as close as possible)."
: > "$LOG"
sleep 8
cp "$LOG" /tmp/macos-trackpoint-deadzone-phase1.log

echo
echo "=== phase 2: normal pressure ==="
echo "For the next 8 seconds, move the TrackPoint normally in the same area."
: > "$LOG"
sleep 8
cp "$LOG" /tmp/macos-trackpoint-deadzone-phase2.log

echo
echo "=== environment ==="
sw_vers
echo
echo "=== phase 1 pointer telemetry ==="
grep "pointer-diag" /tmp/macos-trackpoint-deadzone-phase1.log ||     echo "(no nonzero raw pointer samples were observed)"
echo
echo "=== phase 1 edge-pressure events ==="
grep "edge pressure" /tmp/macos-trackpoint-deadzone-phase1.log || echo "(none)"
echo
echo "=== phase 2 pointer telemetry ==="
grep "pointer-diag" /tmp/macos-trackpoint-deadzone-phase2.log ||     echo "(no nonzero raw pointer samples were observed)"
echo
echo "=== phase 2 edge-pressure events ==="
grep "edge pressure" /tmp/macos-trackpoint-deadzone-phase2.log || echo "(none)"
'
