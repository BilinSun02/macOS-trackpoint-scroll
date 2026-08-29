#!/bin/sh
set -eu

LABEL="io.github.bilinsun02.macos-trackpoint-scroll"
BUNDLE_ID="$LABEL"
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
SOURCE_BIN="$ROOT/build/macOS-trackpoint-scroll"
SOURCE_LAUNCHER="$ROOT/build/macOS-trackpoint-scroll-launcher"
APP_DIR="$HOME/Applications/macOS-trackpoint-scroll.app"
CONTENTS_DIR="$APP_DIR/Contents"
MACOS_DIR="$CONTENTS_DIR/MacOS"
RESOURCES_DIR="$CONTENTS_DIR/Resources"
LAUNCHER_BIN="$MACOS_DIR/macOS-trackpoint-scroll"
LAUNCHER_MARKER="$RESOURCES_DIR/stable-launcher-v1"
INSTALL_DIR="$HOME/Library/Application Support/macOS-trackpoint-scroll"
INSTALL_BIN="$INSTALL_DIR/macOS-trackpoint-scroll"
CONFIG_DIR="$HOME/.config"
CONFIG_PATH="$CONFIG_DIR/macOS-trackpoint-scroll.conf"
EXAMPLE_CONFIG="$ROOT/config/trackpoint-scroll.conf.example"
AGENT_DIR="$HOME/Library/LaunchAgents"
PLIST="$AGENT_DIR/$LABEL.plist"
LOG_DIR="$HOME/Library/Logs/macOS-trackpoint-scroll"
UID_NUM="$(id -u)"
NEW_LAUNCHER=0

if [ ! -x "$SOURCE_BIN" ] || [ ! -x "$SOURCE_LAUNCHER" ]; then
    echo "error: build products are missing; run make first" >&2
    exit 1
fi

mkdir -p "$INSTALL_DIR" "$CONFIG_DIR" "$AGENT_DIR" "$LOG_DIR" "$HOME/Applications"

# The worker is deliberately outside the signed app. It may change on every
# development install without changing the TCC-facing application identity.
cp "$SOURCE_BIN" "$INSTALL_BIN"
chmod 755 "$INSTALL_BIN"

# Create the tiny TCC-facing launcher only once. Subsequent installs leave the
# entire signed app bundle byte-for-byte alone, preserving its code identity.
if [ ! -f "$LAUNCHER_MARKER" ]; then
    NEW_LAUNCHER=1
    rm -rf "$APP_DIR"
    mkdir -p "$MACOS_DIR" "$RESOURCES_DIR"
    cp "$SOURCE_LAUNCHER" "$LAUNCHER_BIN"
    chmod 755 "$LAUNCHER_BIN"

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
    <string>1</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>LSBackgroundOnly</key>
    <true/>
</dict>
</plist>
EOF
    : >"$LAUNCHER_MARKER"
    plutil -lint "$CONTENTS_DIR/Info.plist" >/dev/null
    codesign --force --deep --sign - --identifier "$BUNDLE_ID" "$APP_DIR"
    codesign --verify --deep --strict "$APP_DIR"
fi

if [ ! -e "$CONFIG_PATH" ]; then
    cp "$EXAMPLE_CONFIG" "$CONFIG_PATH"
    chmod 600 "$CONFIG_PATH"
    echo "installed default config: $CONFIG_PATH"
fi

cat >"$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0.dtd" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>$LABEL</string>
    <key>ProgramArguments</key>
    <array>
        <string>$LAUNCHER_BIN</string>
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
launchctl bootout "gui/$UID_NUM" "$PLIST" >/dev/null 2>&1 || true

if [ "$NEW_LAUNCHER" -eq 1 ]; then
    echo "requesting Input Monitoring for the stable launcher..."
    echo "macOS may show a permission prompt; approve macOS-trackpoint-scroll."
    open -W "$APP_DIR" --args --request-input-monitoring || true
fi

launchctl bootstrap "gui/$UID_NUM" "$PLIST"
launchctl kickstart -k "gui/$UID_NUM/$LABEL"

echo "installed and started $LABEL"
echo "stable launcher: $APP_DIR"
echo "worker:          $INSTALL_BIN"
echo "config:          $CONFIG_PATH"
echo "logs:            $LOG_DIR"
echo
echo "The launcher is intentionally not replaced on ordinary reinstalls."
echo "Worker updates therefore do not change the Input Monitoring identity."
echo
echo "status: launchctl print gui/$UID_NUM/$LABEL"
echo "logs:   tail -f '$LOG_DIR/stderr.log'"
