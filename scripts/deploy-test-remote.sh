#!/bin/sh
set -eu

if [ "$#" -lt 1 ]; then
    echo "usage: $0 user@host [remote-dir]" >&2
    exit 2
fi

REMOTE="$1"
REMOTE_DIR="${2:-~/macOS-trackpoint-scroll-test}"
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
VERSION="$(cat "$ROOT/VERSION")"
ARCHIVE="$ROOT/dist/macOS-trackpoint-scroll-$VERSION.tar.gz"
CHECKSUM="$ARCHIVE.sha256"

IDENTITY="${MACOS_TRACKPOINT_CODESIGN_IDENTITY:-}"
if [ -z "$IDENTITY" ]; then
    IDENTITY="$(
        security find-identity -v -p codesigning 2>/dev/null |
        awk '/^[[:space:]]*[0-9]+\)/ { print $2; exit }'
    )"
fi

if [ -z "$IDENTITY" ]; then
    echo "error: no macOS code-signing identity found" >&2
    exit 1
fi

echo "==> building signed package"
echo "    identity hash: $IDENTITY"
security find-identity -v -p codesigning 2>/dev/null |
    awk -v id="$IDENTITY" '$2 == id { sub(/^[^"]*"/, "    authority: "); sub(/"$/, ""); print; exit }' || true
(
    cd "$ROOT"
    make clean
    MACOS_TRACKPOINT_CODESIGN_IDENTITY="$IDENTITY" make dist
)

echo "==> preparing remote directory: $REMOTE:$REMOTE_DIR"
ssh "$REMOTE" "mkdir -p $REMOTE_DIR"

echo "==> copying package"
scp "$ARCHIVE" "$CHECKSUM" "$REMOTE:$REMOTE_DIR/"

ARCHIVE_NAME="$(basename "$ARCHIVE")"
CHECKSUM_NAME="$(basename "$CHECKSUM")"
STAGE_NAME="macOS-trackpoint-scroll-$VERSION"

echo "==> installing on test Mac"
ssh -t "$REMOTE" "
set -eu
cd $REMOTE_DIR
shasum -a 256 -c '$CHECKSUM_NAME'
rm -rf '$STAGE_NAME'
tar -xzf '$ARCHIVE_NAME'
cd '$STAGE_NAME'
./install.sh
"

echo "==> installed app signing identity"
ssh "$REMOTE" '
set -eu
APP="$HOME/Applications/macOS-trackpoint-scroll.app"
echo "=== codesign identity ==="
codesign -dv --verbose=4 "$APP" 2>&1 |
    grep -E "^(Identifier|TeamIdentifier|Authority|CDHash)=" || true
echo "=== designated requirement ==="
codesign -dr - "$APP" 2>&1 || true
'

echo "==> enabling full virtual-HID pointer test mode"
ssh "$REMOTE" 'sh -s' <<'REMOTE_ENABLE'
set -eu
LABEL='io.github.bilinsun02.macos-trackpoint-scroll'
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
DOMAIN="gui/$(id -u)"

launchctl bootout "$DOMAIN" "$PLIST" >/dev/null 2>&1 || true

if ! /usr/libexec/PlistBuddy -c 'Print :ProgramArguments' "$PLIST" |
     grep -q -- '--vhid-pointer'; then
    /usr/libexec/PlistBuddy -c 'Add :ProgramArguments:3 string --vhid-pointer' "$PLIST"
fi

plutil -lint "$PLIST" >/dev/null
launchctl bootstrap "$DOMAIN" "$PLIST"
REMOTE_ENABLE

echo "==> restarting user agent with a fresh log"
ssh "$REMOTE" '
set -eu
LOG="$HOME/Library/Logs/macOS-trackpoint-scroll/stderr.log"
LABEL="io.github.bilinsun02.macos-trackpoint-scroll"
: > "$LOG"
launchctl kickstart -k "gui/$(id -u)/$LABEL"
sleep 3

echo
echo "=== startup diagnostics ==="
grep -E "privacy |IOHIDManagerOpen|diagnosis:|matched HID|running for|raw HID pointer curve|virtual-HID pointer|system natural-scroll|scroll profile=|active .*scroll rewrite tap|VHID scroll carrier rewrite enabled|falling back to accelerated VHID wheel path" "$LOG" || true

if grep -q "privacy .*accessibility=denied" "$LOG"; then
    echo
    echo "ERROR: Accessibility is denied for the installed app."
    echo "The active scroll-rewrite tap cannot run, so this build is still"
    echo "falling back to the known-bad accelerated VHID wheel path."
    echo
    echo "Grant Accessibility to:"
    echo "  $HOME/Applications/macOS-trackpoint-scroll.app"
    exit 4
fi

if ! grep -q "VHID scroll carrier rewrite enabled" "$LOG"; then
    echo
    echo "ERROR: scroll rewrite backend did not arm."
    echo "Do not judge scrolling linearity from this fallback run."
    exit 5
fi
'

echo "==> virtual-HID scroll diagnostics"
ssh -t "$REMOTE" "
sudo grep -E 'scroll-acceleration-support|effective scroll acceleration key|effective-scroll-acceleration|mouse-scroll-acceleration|legacy-scroll-acceleration|requested scroll acceleration disable|disabled scroll acceleration|could not disable scroll acceleration' \
  '/Library/Logs/macOS-trackpoint-scroll/edge-pressure-helper.stderr.log' | tail -20 || true
"

cat <<'EOF'

Installed in full virtual-HID pointer A/B mode with scroll rewrite active.
To collect compact scroll telemetry:

  scripts/test-remote-scroll.sh user@host

EOF
