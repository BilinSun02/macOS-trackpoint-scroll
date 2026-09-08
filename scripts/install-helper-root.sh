#!/bin/sh
set -eu

LABEL="io.github.bilinsun02.macos-trackpoint-scroll.edge-pressure-helper"
HELPER_DIR="/Library/PrivilegedHelperTools"
HELPER_BIN="$HELPER_DIR/$LABEL"
PLIST="/Library/LaunchDaemons/$LABEL.plist"
LOG_DIR="/Library/Logs/macOS-trackpoint-scroll"
KARABINER_SOCKET="/Library/Application Support/org.pqrs/tmp/rootonly/karabiner_virtual_hid_device_service.sock"
KARABINER_DAEMON="/Library/Application Support/org.pqrs/Karabiner-DriverKit-VirtualHIDDevice/Applications/Karabiner-VirtualHIDDevice-Daemon.app/Contents/MacOS/Karabiner-VirtualHIDDevice-Daemon"

if [ "$(id -u)" -ne 0 ]; then
    echo "error: install-helper-root.sh must run as root" >&2
    exit 1
fi

if [ "$#" -ne 2 ]; then
    echo "usage: $0 HELPER_BINARY USER_UID" >&2
    exit 2
fi

SOURCE_HELPER="$1"
USER_UID="$2"

case "$USER_UID" in
    ''|*[!0-9]*)
        echo "error: invalid user uid: $USER_UID" >&2
        exit 2
        ;;
esac

if [ ! -x "$SOURCE_HELPER" ]; then
    echo "error: helper binary is missing: $SOURCE_HELPER" >&2
    exit 1
fi

if [ ! -S "$KARABINER_SOCKET" ] && [ ! -x "$KARABINER_DAEMON" ]; then
    cat >&2 <<'EOF'
error: Karabiner-DriverKit-VirtualHIDDevice is required for Dock edge pressure.

Install/enable Karabiner's virtual HID device first, then rerun the installer.
EOF
    exit 1
fi

mkdir -p "$HELPER_DIR" "$LOG_DIR"
chown root:wheel "$HELPER_DIR" "$LOG_DIR"
chmod 755 "$HELPER_DIR" "$LOG_DIR"

launchctl bootout system "$PLIST" >/dev/null 2>&1 || true

install -o root -g wheel -m 755 "$SOURCE_HELPER" "$HELPER_BIN"

cat >"$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>$LABEL</string>
    <key>ProgramArguments</key>
    <array>
        <string>$HELPER_BIN</string>
        <string>--uid</string>
        <string>$USER_UID</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
    <key>ProcessType</key>
    <string>Interactive</string>
    <key>StandardOutPath</key>
    <string>$LOG_DIR/edge-pressure-helper.stdout.log</string>
    <key>StandardErrorPath</key>
    <string>$LOG_DIR/edge-pressure-helper.stderr.log</string>
    <key>ThrottleInterval</key>
    <integer>10</integer>
</dict>
</plist>
EOF

chown root:wheel "$PLIST"
chmod 644 "$PLIST"
plutil -lint "$PLIST" >/dev/null

launchctl bootstrap system "$PLIST"
launchctl kickstart -k "system/$LABEL"

echo "installed and started $LABEL for uid $USER_UID"
