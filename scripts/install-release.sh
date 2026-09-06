#!/bin/sh
set -eu

LABEL="io.github.bilinsun02.macos-trackpoint-scroll"
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
SOURCE_APP="$ROOT/macOS-trackpoint-scroll.app"
SOURCE_BIN="$SOURCE_APP/Contents/MacOS/macOS-trackpoint-scroll"
APP_DIR="$HOME/Applications/macOS-trackpoint-scroll.app"
INSTALL_BIN="$APP_DIR/Contents/MacOS/macOS-trackpoint-scroll"
LEGACY_INSTALL_DIR="$HOME/Library/Application Support/macOS-trackpoint-scroll"
CONFIG_DIR="$HOME/.config"
CONFIG_PATH="$CONFIG_DIR/macOS-trackpoint-scroll.conf"
EXAMPLE_CONFIG="$ROOT/trackpoint-scroll.conf.example"
AGENT_DIR="$HOME/Library/LaunchAgents"
PLIST="$AGENT_DIR/$LABEL.plist"
LOG_DIR="$HOME/Library/Logs/macOS-trackpoint-scroll"
UID_NUM="$(id -u)"

if [ ! -x "$SOURCE_BIN" ]; then
    echo "error: release bundle is incomplete: $SOURCE_BIN is missing" >&2
    exit 1
fi

mkdir -p "$HOME/Applications" "$CONFIG_DIR" "$AGENT_DIR" "$LOG_DIR"

# Stop an existing installation before replacing the app bundle.
launchctl bootout "gui/$UID_NUM" "$PLIST" >/dev/null 2>&1 || true

rm -rf "$APP_DIR" "$LEGACY_INSTALL_DIR"
ditto "$SOURCE_APP" "$APP_DIR"

# GitHub/browser downloads may carry com.apple.quarantine. This release is
# intentionally not Developer-ID signed or notarized, so clear quarantine only
# from the installed copy after the user explicitly runs this installer.
xattr -dr com.apple.quarantine "$APP_DIR" >/dev/null 2>&1 || true

if [ ! -e "$CONFIG_PATH" ]; then
    cp "$EXAMPLE_CONFIG" "$CONFIG_PATH"
    chmod 600 "$CONFIG_PATH"
    echo "installed default config: $CONFIG_PATH"
fi

cat >"$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>$LABEL</string>
    <key>ProgramArguments</key>
    <array>
        <string>$INSTALL_BIN</string>
        <string>--seize</string>
        <string>--verbose</string>
        <string>--config</string>
        <string>$CONFIG_PATH</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>ProcessType</key>
    <string>Interactive</string>
    <key>StandardOutPath</key>
    <string>$LOG_DIR/stdout.log</string>
    <key>StandardErrorPath</key>
    <string>$LOG_DIR/stderr.log</string>
    <key>ThrottleInterval</key>
    <integer>10</integer>
</dict>
</plist>
EOF

plutil -lint "$PLIST" >/dev/null
launchctl bootstrap "gui/$UID_NUM" "$PLIST"
launchctl kickstart -k "gui/$UID_NUM/$LABEL"

echo "installed and started $LABEL"
echo "application: $APP_DIR"
echo "binary:      $INSTALL_BIN"
echo "config:      $CONFIG_PATH"
echo "logs:        $LOG_DIR"
echo
echo "No Xcode, compiler, or code-signing identity was used on this Mac."
echo
echo "Required macOS privacy grants for the application:"
echo "  System Settings > Privacy & Security > Input Monitoring"
echo "  System Settings > Privacy & Security > Accessibility"
echo "  add/enable: $APP_DIR"
echo
echo "After changing either grant, restart with:"
echo "  launchctl kickstart -k gui/$UID_NUM/$LABEL"
echo
echo "status: launchctl print gui/$UID_NUM/$LABEL"
echo "logs:   tail -f '$LOG_DIR/stderr.log'"
