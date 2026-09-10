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
STARTUP="/tmp/macos-trackpoint-scroll-startup-$.log"

# Preserve startup diagnostics before the timed test truncates the live log.
grep -E "privacy |matched HID|running for|raw HID pointer curve|system natural-scroll|scroll profile=|active .*scroll rewrite tap|VHID scroll carrier rewrite enabled|falling back to accelerated VHID wheel path" "$LOG" > "$STARTUP" || true

# Authenticate before the interaction window begins.
sudo -v

if ! grep -q "VHID scroll carrier rewrite enabled" "$STARTUP"; then
    echo
    echo "=== rewrite backend not active ==="
    cat "$STARTUP" || true
    rm -f "$STARTUP"
    echo
    echo "Refusing to run the gesture test against the old accelerated fallback."
    exit 3
fi

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
printf "VHID wheel carriers: "
grep -Ec "trackpoint: vhid-wheel carrier " "$LOG" || true
printf "VHID wheel fallback reports: "
grep -Ec "trackpoint: vhid-wheel fallback " "$LOG" || true
printf "rewritten wheel events: "
grep -Ec "trackpoint: rewrite-wheel observed-point" "$LOG" || true

echo
echo "rewrite magnitude summary:"
awk '
/trackpoint: rewrite-wheel observed-point/ {
    line=$0
    if (match(line, /observed-point\[h=-?[0-9]+ v=-?[0-9]+\]/)) {
        t=substr(line,RSTART,RLENGTH)
        sub(/^.* v=/,"",t)
        sub(/\]$/,"",t)
        ov=t+0
        oa=(ov<0?-ov:ov)
    } else next

    if (match(line, /target-fixed\[h=[^ ]+ v=[^]]+\]/)) {
        t=substr(line,RSTART,RLENGTH)
        sub(/^.* v=/,"",t)
        sub(/\]$/,"",t)
        tv=t+0
        ta=(tv<0?-tv:tv)
    } else next

    n++
    osum+=oa
    tsum+=ta
    if (n==1 || oa<omin) omin=oa
    if (n==1 || oa>omax) omax=oa
    if (n==1 || ta<tmin) tmin=ta
    if (n==1 || ta>tmax) tmax=ta
}
END {
    if (!n) {
        print "  (none)"
        exit
    }
    printf "  events=%d observed_abs[min=%g max=%g mean=%.2f]\n", n, omin, omax, osum/n
    printf "  target_abs[min=%g max=%g mean=%.3f]\n", tmin, tmax, tsum/n
}' "$LOG"

echo
echo "=== core / posting errors ==="
grep -E "core (gesture transition|feed|tick) failed|virtual-HID .* failed|IOHIDManagerOpen failed" "$LOG" || echo "(none)"

echo
echo "=== daemon startup identity ==="
cat "$STARTUP" || true
rm -f "$STARTUP"


REMOTE_TEST

ssh -t "$REMOTE" "sh '$REMOTE_SCRIPT'; rc=\$?; rm -f '$REMOTE_SCRIPT'; exit \$rc"
trap - EXIT HUP INT TERM
