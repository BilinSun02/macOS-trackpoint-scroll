#!/bin/sh
set -eu

LABEL="io.github.bilinsun02.macos-trackpoint-scroll"
BUNDLE_ID="$LABEL"
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
VERSION="$(cat "$ROOT/VERSION")"
SOURCE_BIN="$ROOT/build/macOS-trackpoint-scroll"
DIST_ROOT="$ROOT/dist"
STAGE="$DIST_ROOT/macOS-trackpoint-scroll-$VERSION"
APP_DIR="$STAGE/macOS-trackpoint-scroll.app"
CONTENTS_DIR="$APP_DIR/Contents"
MACOS_DIR="$CONTENTS_DIR/MacOS"
ARCHIVE="$DIST_ROOT/macOS-trackpoint-scroll-$VERSION.tar.gz"
CHECKSUM="$ARCHIVE.sha256"

if [ ! -x "$SOURCE_BIN" ]; then
    echo "error: $SOURCE_BIN is missing; run make first" >&2
    exit 1
fi

rm -rf "$STAGE" "$ARCHIVE" "$CHECKSUM"
mkdir -p "$MACOS_DIR"

cp "$SOURCE_BIN" "$MACOS_DIR/macOS-trackpoint-scroll"
chmod 755 "$MACOS_DIR/macOS-trackpoint-scroll"

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

# Ad-hoc signing gives the release bundle a valid Mach-O code signature without
# requiring a Developer ID or any certificate on the destination Mac.
codesign --force --sign - --identifier "$BUNDLE_ID" "$APP_DIR"
codesign --verify --strict --verbose=2 "$APP_DIR"

cp "$ROOT/scripts/install-release.sh" "$STAGE/install.sh"
cp "$ROOT/scripts/uninstall-user.sh" "$STAGE/uninstall.sh"
cp "$ROOT/config/trackpoint-scroll.conf.example" "$STAGE/trackpoint-scroll.conf.example"
cp "$ROOT/docs/INSTALLING.md" "$STAGE/INSTALLING.md"
cp "$ROOT/README.md" "$STAGE/README.md"
chmod 755 "$STAGE/install.sh" "$STAGE/uninstall.sh"

(
    cd "$DIST_ROOT"
    ARCHIVE_NAME="$(basename "$ARCHIVE")"
    COPYFILE_DISABLE=1 tar -czf "$ARCHIVE_NAME" "$(basename "$STAGE")"
    shasum -a 256 "$ARCHIVE_NAME" >"$(basename "$CHECKSUM")"
)

echo "release archive: $ARCHIVE"
echo "checksum:        $CHECKSUM"
echo
cat "$CHECKSUM"
