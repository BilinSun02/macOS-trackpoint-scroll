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
echo "=== phase 2: your typical pressure ==="
echo "For the next 8 seconds, use the TrackPoint exactly as you normally would."
echo "Do not intentionally press harder just to make the cursor move."
: > "$LOG"
sleep 8
cp "$LOG" /tmp/macos-trackpoint-deadzone-phase2.log

echo
echo "=== phase 3: strong/overshoot pressure ==="
echo "For the next 8 seconds, press hard enough that movement is definitely produced."
echo "This is intentionally stronger than your usual TrackPoint use."
: > "$LOG"
sleep 8
cp "$LOG" /tmp/macos-trackpoint-deadzone-phase3.log

echo
echo "=== environment ==="
sw_vers

for phase in 1 2 3; do
    echo
    echo "=== phase $phase pointer telemetry ==="
    grep "pointer-diag" "/tmp/macos-trackpoint-deadzone-phase$phase.log" || \
        echo "(no nonzero raw pointer samples were observed)"
    echo
    echo "=== phase $phase edge-pressure events ==="
    grep "edge pressure" "/tmp/macos-trackpoint-deadzone-phase$phase.log" || echo "(none)"
done
'
