#!/bin/sh
set -eu

if [ "$#" -lt 1 ]; then
    echo "usage: $0 user@host" >&2
    exit 2
fi

REMOTE="$1"
REMOTE_SCRIPT="/tmp/macos-trackpoint-scroll-test-$$.sh"

cleanup()
{
    ssh "$REMOTE" "rm -f '$REMOTE_SCRIPT'" >/dev/null 2>&1 || true
}
trap cleanup EXIT HUP INT TERM

# Upload the test body separately so its awk programs and interactive reads do
# not have to survive nested ssh/shell quoting.
ssh "$REMOTE" "cat > '$REMOTE_SCRIPT' && chmod 700 '$REMOTE_SCRIPT'" <<'REMOTE_TEST'
#!/bin/sh
set -eu

LOG="$HOME/Library/Logs/macOS-trackpoint-scroll/stderr.log"

# Authenticate before the interaction window begins.
sudo -v

echo
echo "=== middle-scroll test ==="
echo "For 8 seconds after you press Enter, hold the TrackPoint middle button and"
echo "move the TrackPoint enough that scrolling should clearly occur."
printf "Press Enter to begin: "
IFS= read -r _ </dev/tty

: > "$LOG"
echo "Testing..."
sleep 8
echo "Done."

echo
echo "=== compact scroll summary ==="
printf "middle transitions: "
grep -Ec "trackpoint: middle (down|up)" "$LOG" || true
printf "raw motion samples: "
grep -Ec "trackpoint: raw [xy]=" "$LOG" || true
printf "scroll outputs: "
grep -Ec "trackpoint: scroll x=" "$LOG" || true
printf "VHID wheel reports: "
grep -Ec "trackpoint: vhid-wheel send " "$LOG" || true
printf "observed wheel events: "
grep -Ec "trackpoint: observed-wheel " "$LOG" || true

echo
echo "sent vertical wheel-count distribution:"
awk '
/trackpoint: vhid-wheel send / {
    if (match($0, / v=-?[0-9]+/)) {
        v=substr($0, RSTART+3, RLENGTH-3)+0
        counts[v]++
        seen=1
    }
}
END {
    if (!seen) {
        print "  (none)"
        exit
    }
    for (v in counts)
        printf "  v=%s count=%d\n", v, counts[v]
}' "$LOG"

echo
echo "observed vertical point-delta summary:"
awk '
/trackpoint: observed-wheel / {
    if (match($0, /point\[h=-?[0-9]+ v=-?[0-9]+\]/)) {
        token=substr($0, RSTART, RLENGTH)
        sub(/^.* v=/, "", token)
        sub(/\]$/, "", token)
        v=token+0
        a=(v < 0 ? -v : v)
        if (a > 0) {
            n++
            sum += a
            if (n == 1 || a < min) min=a
            if (n == 1 || a > max) max=a
            if (n <= 5) first[n]=a
            last1=last2
            last2=last3
            last3=last4
            last4=last5
            last5=a
        }
    }
}
END {
    if (!n) {
        print "  (none)"
        exit
    }
    printf "  nonzero events=%d min_abs=%g max_abs=%g mean_abs=%.2f\n",
           n, min, max, sum/n
    printf "  first_abs:"
    for (i=1; i<=n && i<=5; i++)
        printf " %g", first[i]
    printf "\n"
    printf "  last_abs:"
    if (n >= 5)
        printf " %g %g %g %g %g", last1, last2, last3, last4, last5
    else
        for (i=1; i<=n; i++)
            printf " %g", first[i]
    printf "\n"
}' "$LOG"

echo
echo "=== core / posting errors ==="
grep -E "core (gesture transition|feed|tick) failed|virtual-HID .* failed|IOHIDManagerOpen failed" "$LOG" || echo "(none)"

echo
echo "=== daemon startup identity ==="
grep -E "privacy |matched HID|running for|raw HID pointer curve|system natural-scroll|scroll profile=" "$LOG" || true

echo
echo "=== virtual-HID scroll configuration ==="
sudo grep -E "scroll-acceleration-support|effective scroll acceleration key|effective-scroll-acceleration|mouse-scroll-acceleration|legacy-scroll-acceleration|disabled scroll acceleration|could not disable scroll acceleration" \
  "/Library/Logs/macOS-trackpoint-scroll/edge-pressure-helper.stderr.log" | tail -20 || true
REMOTE_TEST

ssh -t "$REMOTE" "sh '$REMOTE_SCRIPT'; status=\$?; rm -f '$REMOTE_SCRIPT'; exit \$status"
trap - EXIT HUP INT TERM
