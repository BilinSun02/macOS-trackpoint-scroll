# Packaging a prebuilt release

This guide is for the development Mac that has the repository, Xcode/Command Line Tools, and the private `trackpoint-scroll-core` submodule available.

The produced release is intentionally **not Developer-ID signed or notarized**. It is ad-hoc signed so a destination Mac does not need Xcode or any Apple code-signing identity.

## 1. Start from the release branch or commit

Make sure the source tree and private submodule are current:

```sh
git pull --ff-only
git submodule update --init --recursive
```

## 2. Build

```sh
make clean
make
```

The executable should now be:

```text
build/macOS-trackpoint-scroll
```

## 3. Create the binary release archive

```sh
make dist
```

`make dist` runs `scripts/package-release.sh`. It:

- creates a minimal `macOS-trackpoint-scroll.app` bundle around the already-built executable;
- ad-hoc signs that app with `codesign --sign -`;
- verifies the resulting signature;
- includes the identity-free installer, uninstaller, default config example, README, and installation guide;
- writes a compressed release archive under `dist/`;
- writes a SHA-256 checksum next to the archive.

For version `1.0.2`, the output names are:

```text
dist/macOS-trackpoint-scroll-1.0.2.tar.gz
dist/macOS-trackpoint-scroll-1.0.2.tar.gz.sha256
```

The version comes from the repository's `VERSION` file.

## 4. Inspect the result

Check the archive contents:

```sh
tar -tzf dist/macOS-trackpoint-scroll-$(cat VERSION).tar.gz
```

Verify the checksum:

```sh
shasum -a 256 -c dist/macOS-trackpoint-scroll-$(cat VERSION).tar.gz.sha256
```

You can also unpack it into a temporary directory and inspect the app signature:

```sh
tmp="$(mktemp -d)"
tar -xzf "dist/macOS-trackpoint-scroll-$(cat VERSION).tar.gz" -C "$tmp"
codesign --verify --strict --verbose=2 \
  "$tmp/macOS-trackpoint-scroll-$(cat VERSION)/macOS-trackpoint-scroll.app"
rm -rf "$tmp"
```

## 5. Test the release installer before publishing

The strongest local test is to install from the packaged archive rather than from the source tree:

```sh
tmp="$(mktemp -d)"
tar -xzf "dist/macOS-trackpoint-scroll-$(cat VERSION).tar.gz" -C "$tmp"
cd "$tmp/macOS-trackpoint-scroll-$(cat VERSION)"
./install.sh
```

Then check:

```sh
launchctl print gui/$(id -u)/io.github.bilinsun02.macos-trackpoint-scroll
tail -f "$HOME/Library/Logs/macOS-trackpoint-scroll/stderr.log"
```

The release installer deliberately does **not** run `make`, `clang`, `security find-identity`, or `codesign`. It only installs the prebuilt app and LaunchAgent.

## 6. Publish

Upload both files from `dist/` to the corresponding GitHub Release:

```text
macOS-trackpoint-scroll-<version>.tar.gz
macOS-trackpoint-scroll-<version>.tar.gz.sha256
```

If GitHub CLI is installed, a typical command is:

```sh
VERSION="$(cat VERSION)"
gh release create "v$VERSION" \
  "dist/macOS-trackpoint-scroll-$VERSION.tar.gz" \
  "dist/macOS-trackpoint-scroll-$VERSION.tar.gz.sha256" \
  --title "macOS TrackPoint Scroll v$VERSION"
```

Do not publish a release until the packaged installer has been tested on at least one clean user installation path.

## Updating a release

When the binary changes, repeat the build and `make dist` steps. Because the release uses an ad-hoc signature instead of Developer ID, macOS privacy controls may treat a newly built release as a different code identity and may require Input Monitoring or Accessibility to be granted again.
