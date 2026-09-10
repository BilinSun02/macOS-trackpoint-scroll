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
grep -E 'privacy |IOHIDManagerOpen|diagnosis:|matched HID|running for|raw HID pointer curve' \"\$LOG\" || true
"

cat <<'EOF'

Installed. To collect pointer telemetry after reproducing the axis-lock behavior,
run from the development Mac:

  scripts/test-remote-diagnostics.sh user@host

EOF
