# Building and installing from source

This is the recommended path when no official prebuilt release is available. It builds, signs, installs, and starts `macOS-trackpoint-scroll` on **your own Mac**.

You do **not** need a paid Apple Developer Program membership for this local source-install workflow. A persistent **Apple Development** identity created by Xcode for your Apple Account is sufficient. Developer ID Application signing and notarization are only needed for polished public distribution of a prebuilt binary.

## 1. Install Xcode and prepare a local signing identity

Install Xcode from the Mac App Store, launch it once, and allow it to install any requested components.

In Xcode:

1. Open **Xcode > Settings > Accounts**.
2. Add/sign in to your Apple Account if it is not already present.
3. Select your account and its team. If you are not enrolled in the paid Apple Developer Program, Xcode will normally show a **Personal Team**.
4. Click **Manage Certificates**.
5. Click the **+** button and create an **Apple Development** certificate.

Apple documents this certificate-management flow here:

- <https://developer.apple.com/documentation/xcode/sharing-your-teams-signing-certificates>

Verify that the resulting identity and private key are usable from Terminal:

```sh
security find-identity -v -p codesigning
```

You should see at least one valid identity, for example:

```text
1) ABCDEF0123456789ABCDEF0123456789ABCDEF01 "Apple Development: ..."
```

The 40-character value is the identity hash. Keep it handy if more than one code-signing identity is installed.

> **Privacy note:** the certificate identity is inspectable in any signed app you give to somebody else and may contain personal information such as the Apple Account email used for development. That is harmless for a build that stays on your own Mac, but inspect the signing identity before sharing a privately packaged build.

## 2. Install Karabiner-Elements and enable its virtual HID driver

The installed TrackPoint path uses Karabiner's DriverKit virtual mouse as the output device. Install Karabiner-Elements using its official installer:

- <https://karabiner-elements.pqrs.org/docs/getting-started/installation/>

Or, if you already use Homebrew:

```sh
brew install --cask karabiner-elements
```

Open Karabiner-Elements Settings once and complete its macOS setup prompts. In particular, allow its background services and DriverKit virtual HID extension. Current Karabiner-Elements documentation describes the required macOS settings here:

- <https://karabiner-elements.pqrs.org/docs/manual/misc/required-macos-settings/>

The TrackPoint installer checks that the Karabiner virtual-HID service is available before installing its own privileged bridge.

## 3. Clone the repository with its pinned core

```sh
git clone --recurse-submodules https://github.com/BilinSun02/macOS-trackpoint-scroll.git
cd macOS-trackpoint-scroll
```

If you cloned without submodules, repair the checkout with:

```sh
git submodule update --init --recursive
```

## 4. Build

```sh
make
```

A successful build produces:

```text
build/macOS-trackpoint-scroll
build/macOS-trackpoint-scroll-edge-pressure-helper
```

An optional CLI smoke test is:

```sh
./build/macOS-trackpoint-scroll --help
```

## 5. Install and sign the local app

If `security find-identity -v -p codesigning` lists only the identity you want to use, the simple path is:

```sh
make install-user
```

If you have multiple signing identities, explicitly select the desired identity by its 40-character hash:

```sh
MACOS_TRACKPOINT_CODESIGN_IDENTITY="<identity-hash>" make install-user
```

`make install-user` rebuilds if necessary, then:

- creates `~/Applications/macOS-trackpoint-scroll.app`;
- signs the nested helper and app with the selected persistent identity;
- prompts for `sudo` to install the narrow root virtual-HID bridge;
- installs a per-user LaunchAgent with `KeepAlive` enabled;
- installs the default config if one does not already exist;
- starts the root helper and user daemon.

Ad-hoc signing is intentionally not used for the source install because a persistent signing identity gives macOS a stable code identity for privacy/TCC authorization across rebuilds.

## 6. Grant macOS privacy permissions

Open:

```text
System Settings > Privacy & Security > Input Monitoring
System Settings > Privacy & Security > Accessibility
```

Add or enable:

```text
~/Applications/macOS-trackpoint-scroll.app
```

Then restart the user agent:

```sh
launchctl kickstart -k gui/$(id -u)/io.github.bilinsun02.macos-trackpoint-scroll
```

The daemon requests the relevant permissions at startup and logs their effective state separately. Do not rely only on the System Settings checkbox.

## 7. Verify the installed services

User daemon:

```sh
launchctl print gui/$(id -u)/io.github.bilinsun02.macos-trackpoint-scroll
```

User-daemon log:

```sh
tail -n 100 "$HOME/Library/Logs/macOS-trackpoint-scroll/stderr.log"
```

Root virtual-HID bridge:

```sh
sudo launchctl print system/io.github.bilinsun02.macos-trackpoint-scroll.edge-pressure-helper
```

A healthy startup should show that the target HID device was matched, the Karabiner virtual-HID path is available, and the active scroll-rewrite tap is enabled. It should also report successful Input Monitoring / CoreGraphics PostEvent / Accessibility authorization after those permissions have been granted.

Exercise pointer motion, left/right clicks, middle-button scrolling, and display-edge/Dock behavior before treating the installation as complete.

## 8. Updating a source install

From the repository checkout:

```sh
git pull --ff-only
git submodule update --init --recursive
make clean
make install-user
```

If you have multiple signing identities, continue passing the same explicit `MACOS_TRACKPOINT_CODESIGN_IDENTITY` so the installed app keeps the same signing identity across updates.

## 9. Optional: make your own archive

You do **not** need an archive merely to use the software on the Mac where you built it. `make install-user` is the normal source-install path.

If you specifically want a packaged archive for your own testing or controlled sharing, use the same signing identity:

```sh
MACOS_TRACKPOINT_CODESIGN_IDENTITY="<identity-hash>" make dist
```

The archive and checksum are written under `dist/`. Verify the checksum from that directory:

```sh
VERSION="$(cat VERSION)"
(
  cd dist
  shasum -a 256 -c "macOS-trackpoint-scroll-$VERSION.tar.gz.sha256"
)
```

Before giving such an archive to anybody else, inspect the certificate identity that recipients will be able to see:

```sh
VERSION="$(cat VERSION)"
codesign -dv --verbose=4 \
  "dist/macOS-trackpoint-scroll-$VERSION/macOS-trackpoint-scroll.app" 2>&1 |
  grep -E '^(Authority|TeamIdentifier|Identifier)='
```

An Apple Development-signed archive is suitable for controlled testing, but it is **not** the normal public-distribution path. For a broadly published prebuilt release, use **Developer ID Application** signing and notarization as described in [PACKAGING.md](PACKAGING.md). That public-distribution path requires the appropriate Apple Developer Program membership.

## Uninstall

From the source checkout:

```sh
make uninstall-user
```

The uninstaller removes the LaunchAgent, installed app, root helper, and its LaunchDaemon while intentionally leaving configuration and logs in place.
