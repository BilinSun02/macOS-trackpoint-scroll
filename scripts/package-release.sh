#!/bin/sh
set -eu

LABEL="io.github.bilinsun02.macos-trackpoint-scroll"
BUNDLE_ID="$LABEL"
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
VERSION="$(cat "$ROOT/VERSION")"
SOURCE_BIN="$ROOT/build/macOS-trackpoint-scroll"
SOURCE_HELPER="$ROOT/build/macOS-trackpoint-scroll-edge-pressure-helper"
DIST_ROOT="$ROOT/dist"
STAGE="$DIST_ROOT/macOS-trackpoint-scroll-$VERSION"
APP_DIR="$STAGE/macOS-trackpoint-scroll.app"
CONTENTS_DIR="$APP_DIR/Contents"
MACOS_DIR="$CONTENTS_DIR/MacOS"
HELPERS_DIR="$CONTENTS_DIR/Helpers"
BUNDLE_HELPER="$HELPERS_DIR/macOS-trackpoint-scroll-edge-pressure-helper"
ARCHIVE="$DIST_ROOT/macOS-trackpoint-scroll-$VERSION.tar.gz"
CHECKSUM="$ARCHIVE.sha256"

if [ ! -x "$SOURCE_BIN" ]; then
    echo "error: $SOURCE_BIN is missing; run make first" >&2
    exit 1
fi
if [ ! -x "$SOURCE_HELPER" ]; then
    echo "error: $SOURCE_HELPER is missing; run make first" >&2
    exit 1
fi

rm -rf "$STAGE" "$ARCHIVE" "$CHECKSUM"
mkdir -p "$MACOS_DIR" "$HELPERS_DIR"

cp "$SOURCE_BIN" "$MACOS_DIR/macOS-trackpoint-scroll"
cp "$SOURCE_HELPER" "$BUNDLE_HELPER"
chmod 755 "$MACOS_DIR/macOS-trackpoint-scroll" "$BUNDLE_HELPER"

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

# Sign nested code first, then the app bundle.
#
# For real distribution, set MACOS_TRACKPOINT_CODESIGN_IDENTITY to a stable
# Apple-issued identity (Developer ID Application for public distribution).
# Ad-hoc signing remains available as a CI/package-integrity fallback, but its
# designated requirement is tied to the exact build and is unsuitable for
# stable macOS privacy/TCC authorization across releases.
SIGN_IDENTITY="${MACOS_TRACKPOINT_CODESIGN_IDENTITY:-}"
if [ -z "$SIGN_IDENTITY" ]; then
    SIGN_IDENTITY="-"
    echo "warning: packaging with an ad-hoc signature; TCC permissions will not have a stable code identity" >&2
else
    echo "release code-signing identity: $SIGN_IDENTITY"
fi

codesign --force --sign "$SIGN_IDENTITY" "$BUNDLE_HELPER"
codesign --force --sign "$SIGN_IDENTITY" --identifier "$BUNDLE_ID" "$APP_DIR"
codesign --verify --strict --verbose=2 "$BUNDLE_HELPER"
codesign --verify --strict --verbose=2 "$APP_DIR"

echo "app designated requirement:"
codesign --display --requirements - "$APP_DIR" 2>&1 |
    sed -n 's/^designated => /  /p'

cp "$ROOT/scripts/install-release.sh" "$STAGE/install.sh"
cp "$ROOT/scripts/uninstall-user.sh" "$STAGE/uninstall.sh"
cp "$ROOT/scripts/install-helper-root.sh" "$STAGE/install-helper-root.sh"
cp "$ROOT/scripts/uninstall-helper-root.sh" "$STAGE/uninstall-helper-root.sh"
cp "$ROOT/config/trackpoint-scroll.conf.example" "$STAGE/trackpoint-scroll.conf.example"
cp "$ROOT/docs/INSTALLING.md" "$STAGE/INSTALLING.md"
cp "$ROOT/README.md" "$STAGE/README.md"
chmod 755 "$STAGE/install.sh" "$STAGE/uninstall.sh" \
    "$STAGE/install-helper-root.sh" "$STAGE/uninstall-helper-root.sh"

# Release artifacts must not capture a developer-machine home directory.
# Scan text and binary content alike so embedded absolute paths are caught.
if grep -R -a -n '/Users/' "$STAGE" >/dev/null 2>&1; then
    echo "error: release staging tree contains a machine-specific /Users/... path:" >&2
    grep -R -a -n '/Users/' "$STAGE" >&2 || true
    exit 1
fi

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
