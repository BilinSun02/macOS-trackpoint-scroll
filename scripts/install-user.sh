#!/bin/sh
set -eu

LABEL="io.github.bilinsun02.macos-trackpoint-scroll"
SOURCE_BIN="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)/build/macOS-trackpoint-scroll"
INSTALL_DIR="$HOME/Library/Application Support/macOS-trackpoint-scroll"
INSTALL_BIN="$INSTALL_DIR/macOS-trackpoint-scroll"
OLD_APP_DIR="$HOME/Applications/macOS-trackpoint-scroll.app"
CONFIG_DIR="$HOME/.config"
CONFIG_PATH="$CONFIG_DIR/macOS-trackpoint-scroll.conf"
EXAMPLE_CONFIG="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)/config/trackpoint-scroll.conf.example"
AGENT_DIR="$HOME/Library/LaunchAgents"
PLIST="$AGENT_DIR/$LABEL.plist"
LOG_DIR="$HOME/Library/Logs/macOS-trackpoint-scroll"
UID_NUM="$(id -u)"

if [ ! -x "$SOURCE_BIN" ]; then
    echo "error: $SOURCE_BIN is missing; run make first" >&2
    exit 1
fi

mkdir -p "$INSTALL_DIR" "$CONFIG_DIR" "$AGENT_DIR" "$LOG_DIR"

# Remove the temporary app-bundle installation used while diagnosing TCC.
rm -rf "$OLD_APP_DIR"

cp "$SOURCE_BIN" "$INSTALL_BIN"
chmod 755 "$INSTALL_BIN"

# An ordinary ad-hoc signature gets a designated requirement tied to the exact
# build, so TCC/Input Monitoring can treat every rebuild as different code.
# Give this private per-user utility an explicit stable designated requirement.
# This is intentionally not distribution-grade identity security: another local
# binary could deliberately copy the same requirement.
DR="designated => identifier \"$LABEL\""
codesign --force --sign - \
    --identifier "$LABEL" \
    --requirements "=$DR" \
    "$INSTALL_BIN"

# Fail installation if signing did not produce the expected stable DR.
codesign --verify --strict "$INSTALL_BIN"
codesign --display --requirements - "$INSTALL_BIN" 2>&1 | \
    grep -F "designated => identifier \"$LABEL\"" >/dev/null

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
echo "binary: $INSTALL_BIN"
echo "config: $CONFIG_PATH"
echo "logs:   $LOG_DIR"
echo
echo "The executable uses a stable explicit designated requirement for TCC."
echo "Because this install migrates away from the temporary .app identity, macOS"
echo "may require one final Input Monitoring authorization for:"
echo "  $INSTALL_BIN"
echo "Subsequent rebuilds should retain the same designated requirement."
echo
echo "status: launchctl print gui/$UID_NUM/$LABEL"
echo "logs:   tail -f '$LOG_DIR/stderr.log'"
