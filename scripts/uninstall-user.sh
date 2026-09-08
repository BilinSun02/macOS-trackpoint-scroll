#!/bin/sh
set -eu

LABEL="io.github.bilinsun02.macos-trackpoint-scroll"
APP_DIR="$HOME/Applications/macOS-trackpoint-scroll.app"
LEGACY_INSTALL_DIR="$HOME/Library/Application Support/macOS-trackpoint-scroll"
AGENT_DIR="$HOME/Library/LaunchAgents"
PLIST="$AGENT_DIR/$LABEL.plist"
UID_NUM="$(id -u)"
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." 2>/dev/null && pwd || CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
ROOT_UNINSTALLER="$ROOT/scripts/uninstall-helper-root.sh"
if [ ! -f "$ROOT_UNINSTALLER" ]; then
    ROOT_UNINSTALLER="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/uninstall-helper-root.sh"
fi

launchctl bootout "gui/$UID_NUM" "$PLIST" >/dev/null 2>&1 || true
rm -f "$PLIST"
rm -rf "$APP_DIR" "$LEGACY_INSTALL_DIR"

if [ -f "$ROOT_UNINSTALLER" ]; then
    echo "Removing the root edge-pressure helper (sudo required)..."
    sudo sh "$ROOT_UNINSTALLER"
else
    echo "warning: root helper uninstaller not found; privileged helper was left installed" >&2
fi

echo "uninstalled $LABEL"
echo "configuration and logs were left in place"
