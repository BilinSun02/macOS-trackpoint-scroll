#!/bin/sh
set -eu

LABEL="io.github.bilinsun02.macos-trackpoint-scroll"
BUNDLE_ID="$LABEL"
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
VERSION="$(cat "$ROOT/VERSION")"
SOURCE_BIN="$ROOT/build/macOS-trackpoint-scroll"
SOURCE_HELPER="$ROOT/build/macOS-trackpoint-scroll-edge-pressure-helper"
APP_DIR="$HOME/Applications/macOS-trackpoint-scroll.app"
CONTENTS_DIR="$APP_DIR/Contents"
MACOS_DIR="$CONTENTS_DIR/MacOS"
HELPERS_DIR="$CONTENTS_DIR/Helpers"
INSTALL_BIN="$MACOS_DIR/macOS-trackpoint-scroll"
BUNDLE_HELPER="$HELPERS_DIR/macOS-trackpoint-scroll-edge-pressure-helper"
LEGACY_INSTALL_DIR="$HOME/Library/Application Support/macOS-trackpoint-scroll"
CONFIG_DIR="$HOME/.config"
CONFIG_PATH="$CONFIG_DIR/macOS-trackpoint-scroll.conf"
EXAMPLE_CONFIG="$ROOT/config/trackpoint-scroll.conf.example"
AGENT_DIR="$HOME/Library/LaunchAgents"
PLIST="$AGENT_DIR/$LABEL.plist"
LOG_DIR="$HOME/Library/Logs/macOS-trackpoint-scroll"
UID_NUM="$(id -u)"

if [ ! -x "$SOURCE_BIN" ]; then
    echo "error: $SOURCE_BIN is missing; run make first" >&2
    exit 1
fi
if [ ! -x "$SOURCE_HELPER" ]; then
    echo "error: $SOURCE_HELPER is missing; run make first" >&2
    exit 1
fi

SIGN_IDENTITY="${MACOS_TRACKPOINT_CODESIGN_IDENTITY:-}"
if [ -z "$SIGN_IDENTITY" ]; then
    SIGN_IDENTITY="$(security find-identity -v -p codesigning 2>/dev/null | \
        awk '/^[[:space:]]*[0-9]+\)/ { print $2; exit }')"
fi

if [ -z "$SIGN_IDENTITY" ]; then
    cat >&2 <<'EOF'
error: no persistent macOS code-signing identity is available.

Create an Apple Development certificate in Xcode, verify that
  security find-identity -v -p codesigning
lists at least one valid identity, then run make install-user again.
EOF
    exit 1
fi

mkdir -p "$MACOS_DIR" "$HELPERS_DIR" "$CONFIG_DIR" "$AGENT_DIR" "$LOG_DIR" "$HOME/Applications"
rm -rf "$LEGACY_INSTALL_DIR"

# Stop the previous agent before replacing/signing the app.
launchctl bootout "gui/$UID_NUM" "$PLIST" >/dev/null 2>&1 || true

cp "$SOURCE_BIN" "$INSTALL_BIN"
cp "$SOURCE_HELPER" "$BUNDLE_HELPER"
chmod 755 "$INSTALL_BIN" "$BUNDLE_HELPER"

cat >"$CONTENTS_DIR/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleIdentifier</key>
    <string>$BUNDLE_ID</string>
    <key>CFBundleName</key>
    <string>macOS-trackpoint-scroll</string>
    <key>CFBundleDisplayName</key>
    <string>macOS-trackpoint-scroll</string>
    <key>CFBundleExecutable</key>
    <string>macOS-trackpoint-scroll</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleVersion</key>
    <string>$VERSION</string>
    <key>CFBundleShortVersionString</key>
    <string>$VERSION</string>
    <key>LSBackgroundOnly</key>
    <true/>
    <key>NSInputMonitoringUsageDescription</key>
    <string>TrackPoint scrolling and pointer handling require access to this mouse's HID input.</string>
</dict>
</plist>
EOF

plutil -lint "$CONTENTS_DIR/Info.plist" >/dev/null

# Sign the nested privileged helper first, then the containing app. A persistent
# certificate plus the stable bundle identifier gives TCC a stable app identity
# across rebuilds. Ad-hoc signing is intentionally not supported here.
codesign --force --sign "$SIGN_IDENTITY" "$BUNDLE_HELPER"
codesign --force --sign "$SIGN_IDENTITY" --identifier "$BUNDLE_ID" "$APP_DIR"
codesign --verify --strict --verbose=2 "$APP_DIR"

echo "code-signing identity:"
codesign --display --verbose=1 "$APP_DIR" 2>&1 | grep -E '^(Identifier|Authority|TeamIdentifier)=' || true
echo "designated requirement:"
codesign --display --requirements - "$APP_DIR" 2>&1 | sed -n 's/^designated => /  /p'

echo "Installing the root edge-pressure helper (sudo required)..."
sudo sh "$ROOT/scripts/install-helper-root.sh" "$BUNDLE_HELPER" "$UID_NUM"

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
    <key>AssociatedBundleIdentifiers</key>
    <string>$LABEL</string>
    <key>ProgramArguments</key>
    <array>
        <string>$INSTALL_BIN</string>
        <string>--seize</string>
        <string>--edge-pressure-helper</string>
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

echo "installed and started $LABEL v$VERSION"
echo "application: $APP_DIR"
echo "binary:      $INSTALL_BIN"
echo "config:      $CONFIG_PATH"
echo "logs:        $LOG_DIR"
echo "edge helper: /Library/PrivilegedHelperTools/io.github.bilinsun02.macos-trackpoint-scroll.edge-pressure-helper"
echo
echo "Required macOS privacy grants for the application:"
echo "  System Settings > Privacy & Security > Input Monitoring"
echo "  System Settings > Privacy & Security > Accessibility"
echo "  add/enable: $APP_DIR"
echo
echo "Input Monitoring permits exclusive HID access; Accessibility permits the"
echo "synthetic Quartz pointer/scroll events that replace the seized mouse events."
echo "After changing either grant, restart with:"
echo "  launchctl kickstart -k gui/$UID_NUM/$LABEL"
echo
echo "status: launchctl print gui/$UID_NUM/$LABEL"
echo "logs:   tail -f '$LOG_DIR/stderr.log'"

echo
echo "edge helper status:"
echo "  sudo launchctl print system/io.github.bilinsun02.macos-trackpoint-scroll.edge-pressure-helper"
echo "edge helper logs:"
echo "  sudo tail -f '/Library/Logs/macOS-trackpoint-scroll/edge-pressure-helper.stderr.log'"
