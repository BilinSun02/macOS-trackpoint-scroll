# Packaging a prebuilt release

This guide is for the development Mac that has the repository, Xcode/Command Line Tools, the `trackpoint-scroll-core` submodule, and a stable Apple-issued code-signing identity.

The destination Mac does **not** need Xcode, a compiler, or a signing identity. Packaging and signing happen before the archive is copied or published.

## Signing rule

Do not publish an ad-hoc-signed build as the normal release artifact. macOS privacy/TCC authorization is tied to code identity; a rebuilt ad-hoc app has an unstable designated requirement and can fail to match apparently enabled Input Monitoring/PostEvent/Accessibility grants.

For controlled development/test distribution, a stable Apple Development identity is usable. For public distribution, use Developer ID Application signing and notarization.

Check available identities:

```sh
security find-identity -v -p codesigning
```

`scripts/package-release.sh` requires `MACOS_TRACKPOINT_CODESIGN_IDENTITY` by default. Ad-hoc packaging remains available only when explicitly requested with `MACOS_TRACKPOINT_ALLOW_ADHOC=1`; treat that output as diagnostic/CI material, not a normal release.

## Build and package

Start from the intended release commit and current submodule pin:

```sh
git pull --ff-only
git submodule update --init --recursive
make clean
make
```

Choose a signing identity and create the archive:

```sh
IDENTITY="$(
  security find-identity -v -p codesigning |
  awk '/^[[:space:]]*[0-9]+\)/ { print $2; exit }'
)"

test -n "$IDENTITY" || {
  echo "No code-signing identity found" >&2
  exit 1
}

MACOS_TRACKPOINT_CODESIGN_IDENTITY="$IDENTITY" make dist
```

The package script:

- creates a minimal `macOS-trackpoint-scroll.app`;
- embeds the privileged virtual-HID helper under `Contents/Helpers`;
- signs nested code first and then the app bundle;
- verifies both signatures and prints the app designated requirement;
- includes the installer/uninstaller, helper installer/uninstaller, config example, README, and installation guide;
- rejects staged content containing a developer-machine `/Users/...` path;
- writes the archive and SHA-256 checksum under `dist/`.

Output names are derived from `VERSION`:

```text
dist/macOS-trackpoint-scroll-<version>.tar.gz
dist/macOS-trackpoint-scroll-<version>.tar.gz.sha256
```

## Inspect the result

```sh
VERSION="$(cat VERSION)"
tar -tzf "dist/macOS-trackpoint-scroll-$VERSION.tar.gz"
shasum -a 256 -c "dist/macOS-trackpoint-scroll-$VERSION.tar.gz.sha256"
```

Inspect the installed code identity from an unpacked copy:

```sh
VERSION="$(cat VERSION)"
tmp="$(mktemp -d)"
tar -xzf "dist/macOS-trackpoint-scroll-$VERSION.tar.gz" -C "$tmp"
APP="$tmp/macOS-trackpoint-scroll-$VERSION/macOS-trackpoint-scroll.app"

codesign --verify --strict --verbose=2 \
  "$APP/Contents/Helpers/macOS-trackpoint-scroll-edge-pressure-helper"
codesign --verify --strict --verbose=2 "$APP"
codesign --display --requirements - "$APP" 2>&1

rm -rf "$tmp"
```

For a stable signed build, the designated requirement should be based on the application identifier plus the Apple-issued signing identity, not only a build-specific `cdhash`.

## Test the packaged installer

Do not treat `make install-user` as sufficient release validation. Test the exact archive that would be published, ideally on a second Mac without Xcode.

After installation, verify:

```sh
launchctl print gui/$(id -u)/io.github.bilinsun02.macos-trackpoint-scroll
sudo launchctl print system/io.github.bilinsun02.macos-trackpoint-scroll.edge-pressure-helper
```

Use a fresh startup log when checking permissions and service health:

```sh
LOG="$HOME/Library/Logs/macOS-trackpoint-scroll/stderr.log"
: > "$LOG"
launchctl kickstart -k gui/$(id -u)/io.github.bilinsun02.macos-trackpoint-scroll
sleep 2
cat "$LOG"
```

A healthy installed path reports Input Monitoring, CoreGraphics PostEvent access, and AX trust; creates the active scroll-rewrite tap; connects to the virtual-HID helper; and seizes the target HID device.

Exercise at minimum:

- gentle, diagonal, and fast pointer motion;
- left/right clicks and click-drag;
- immediate inward reversal at display edges;
- repeated Dock reveal, including after more than 30 seconds idle;
- slow/medium/fast middle-button scrolling in both axes;
- configured natural-scroll direction;
- one LaunchAgent restart followed by the same smoke test.

The idle Dock test exercises the Karabiner virtual-HID heartbeat/reconnect lifecycle.

## Publish

Upload both files under `dist/` to the matching GitHub Release. If GitHub CLI is installed:

```sh
VERSION="$(cat VERSION)"
gh release create "v$VERSION" \
  "dist/macOS-trackpoint-scroll-$VERSION.tar.gz" \
  "dist/macOS-trackpoint-scroll-$VERSION.tar.gz.sha256" \
  --title "macOS TrackPoint Scroll v$VERSION"
```

Do not publish until the packaged installer—not merely the source install—has passed the hardware smoke test.

## Diagnostic-only ad-hoc package

For CI or investigation without a local certificate:

```sh
MACOS_TRACKPOINT_ALLOW_ADHOC=1 make dist
```

Expect privacy authorization to be build-specific. Do not use such an artifact to judge whether TCC grants survive an update.
