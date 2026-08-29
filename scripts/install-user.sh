#!/bin/sh
set -eu

LABEL="io.github.bilinsun02.macos-trackpoint-scroll"
BUNDLE_ID="$LABEL"
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
SOURCE_BIN="$ROOT/build/macOS-trackpoint-scroll"
APP_DIR="$HOME/Applications/macOS-trackpoint-scroll.app"
CONTENTS_DIR="$APP_DIR/Contents"
MACOS_DIR="$CONTENTS_DIR/MacOS"
INSTALL_BIN="$MACOS_DIR/macOS-trackpoint-scroll"
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

# TCC does not preserve privacy grants reliably across ad-hoc-signed rebuilds.
# Use one persistent certificate for every development build. An explicit
# identity can be supplied via MACOS_TRACKPOINT_CODESIGN_IDENTITY; otherwise
# use the first valid code-signing identity in the user's keychains.
SIGN_IDENTITY="${MACOS_TRACKPOINT_CODESIGN_IDENTITY:-}"
if [ -z "$SIGN_IDENTITY" ]; then
    SIGN_IDENTITY="$(security find-identity -v -p codesigning 2>/dev/null | \
        awk '/^[[:space:]]*[0-9]+\)/ { print $2; exit }')"
fi

if [ -z "$SIGN_IDENTITY" ]; then
    cat >&2 <<'EOF'
error: no persistent macOS code-signing identity is available.

Ad-hoc signing is intentionally not used because macOS TCC treats each rebuild
as a different program and forgets Input Monitoring authorization.

If Xcode already has an Apple Development certificate, this command should list it:
  security find-identity -v -p codesigning

Otherwise create an Apple Development certificate in Xcode:
  Xcode > Settings > Accounts > Manage Certificates > + > Apple Development

Then run make install-user again.
EOF
    exit 1
fi

mkdir -p "$MACOS_DIR" "$CONFIG_DIR" "$AGENT_DIR" "$LOG_DIR" "$HOME/Applications"
rm -rf "$LEGACY_INSTALL_DIR"

cp "$SOURCE_BIN" "$INSTALL_BIN"
chmod 755 "$INSTALL_BIN"

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

plutil -lint "$CONTENTS_DIR/Info.plist" >/dev/null

# The actual TrackPoint process carries the stable signing identity. Rebuilding
# changes its code hash, but not its designated requirement (signer + bundle ID).
codesign --force --sign "$SIGN_IDENTITY" --identifier "$BUNDLE_ID" "$APP_DIR"
codesign --verify --strict --verbose=2 "$APP_DIR"

echo "code-signing identity:"
codesign --display --verbose=1 "$APP_DIR" 2>&1 | grep -E '^(Identifier|Authority|TeamIdentifier)=' || true
echo "designated requirement:"
codesign --display --requirements - "$APP_DIR" 2>&1 | sed -n 's/^designated => /  /p'

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
launchctl bootout "gui/$UID_NUM" "$PLIST" >/dev/null 2>&1 || true
launchctl bootstrap "gui/$UID_NUM" "$PLIST"
launchctl kickstart -k "gui/$UID_NUM/$LABEL"

echo "installed and started $LABEL"
echo "application: $APP_DIR"
echo "binary:      $INSTALL_BIN"
echo "config:      $CONFIG_PATH"
echo "logs:        $LOG_DIR"
echo
echo "If Input Monitoring is not yet authorized, add this application in:"
echo "  System Settings > Privacy & Security > Input Monitoring"
echo "  $APP_DIR"
echo
echo "Once authorized, future builds signed with the same identity should retain it."
echo "status: launchctl print gui/$UID_NUM/$LABEL"
echo "logs:   tail -f '$LOG_DIR/stderr.log'"
