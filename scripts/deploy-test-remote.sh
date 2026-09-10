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
echo "    identity: $IDENTITY"
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

echo "==> enabling full virtual-HID pointer test mode"
ssh "$REMOTE" "
set -eu
LABEL='io.github.bilinsun02.macos-trackpoint-scroll'
PLIST=\"\$HOME/Library/LaunchAgents/\$LABEL.plist\"
DOMAIN=\"gui/\$(id -u)\"

launchctl bootout \"\$DOMAIN\" \"\$PLIST\" >/dev/null 2>&1 || true

if ! /usr/libexec/PlistBuddy -c 'Print :ProgramArguments' \"\$PLIST\" |
     grep -q -- '--vhid-pointer'; then
    /usr/libexec/PlistBuddy -c 'Add :ProgramArguments:3 string --vhid-pointer' \"\$PLIST\"
fi

plutil -lint \"\$PLIST\" >/dev/null
launchctl bootstrap \"\$DOMAIN\" \"\$PLIST\"
"

echo "==> restarting user agent with a fresh log"
ssh "$REMOTE" "
set -eu
LOG=\"\$HOME/Library/Logs/macOS-trackpoint-scroll/stderr.log\"
LABEL='io.github.bilinsun02.macos-trackpoint-scroll'
: > \"\$LOG\"
launchctl kickstart -k \"gui/\$(id -u)/\$LABEL\"
sleep 3
echo
echo '=== startup diagnostics ==='
grep -E 'privacy |IOHIDManagerOpen|diagnosis:|matched HID|running for|raw HID pointer curve|virtual-HID pointer|system natural-scroll' \"\$LOG\" || true
"

echo "==> helper scroll diagnostics"
ssh -t "$REMOTE" "
sudo grep -E 'direct floating-point HID scroll ready|direct HID scroll SPI unavailable|cannot create HID event-system client|direct HID scroll forwarding failed' \
  '/Library/Logs/macOS-trackpoint-scroll/edge-pressure-helper.stderr.log' | tail -20 || true
"

cat <<'EOF'

Installed in full virtual-HID pointer A/B mode.
To collect pointer telemetry after reproducing any remaining pointer problem,
run from the development Mac:

  scripts/test-remote-diagnostics.sh user@host

EOF
