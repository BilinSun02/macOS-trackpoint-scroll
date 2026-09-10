#!/bin/sh
set -eu

if [ "$#" -lt 1 ]; then
    echo "usage: $0 user@host" >&2
    exit 2
fi

REMOTE="$1"

ssh "$REMOTE" '
LOG="$HOME/Library/Logs/macOS-trackpoint-scroll/stderr.log"
echo "Move the TrackPoint diagonally/circularly now, especially while reproducing the axis lock."
: > "$LOG"
sleep 12
echo
echo "=== pointer telemetry ==="
grep "pointer-diag" "$LOG" || true
echo
echo "=== edge-pressure telemetry ==="
grep "edge pressure" "$LOG" || true
'
