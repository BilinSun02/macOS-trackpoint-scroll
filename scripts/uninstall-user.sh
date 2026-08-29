#!/bin/sh
set -eu

LABEL="io.github.bilinsun02.macos-trackpoint-scroll"
APP_DIR="$HOME/Applications/macOS-trackpoint-scroll.app"
LEGACY_INSTALL_DIR="$HOME/Library/Application Support/macOS-trackpoint-scroll"
AGENT_DIR="$HOME/Library/LaunchAgents"
PLIST="$AGENT_DIR/$LABEL.plist"
UID_NUM="$(id -u)"

launchctl bootout "gui/$UID_NUM" "$PLIST" >/dev/null 2>&1 || true
rm -f "$PLIST"
rm -rf "$APP_DIR" "$LEGACY_INSTALL_DIR"

echo "uninstalled $LABEL"
echo "configuration and logs were left in place"
