#!/bin/sh
set -eu

LABEL="io.github.bilinsun02.macos-trackpoint-scroll.edge-pressure-helper"
HELPER_BIN="/Library/PrivilegedHelperTools/$LABEL"
PLIST="/Library/LaunchDaemons/$LABEL.plist"
SOCKET_PREFIX="/var/run/io.github.bilinsun02.macos-trackpoint-scroll.edge-pressure."

if [ "$(id -u)" -ne 0 ]; then
    echo "error: uninstall-helper-root.sh must run as root" >&2
    exit 1
fi

launchctl bootout system "$PLIST" >/dev/null 2>&1 || true
rm -f "$PLIST" "$HELPER_BIN"
rm -f "$SOCKET_PREFIX"*.sock 2>/dev/null || true

echo "uninstalled $LABEL"
