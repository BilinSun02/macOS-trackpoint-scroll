# Installing a prebuilt release

The prebuilt release is intended for a Mac that does **not** have Xcode, a compiler toolchain, or an Apple code-signing identity.

It is ad-hoc signed rather than Developer-ID signed or notarized. The installer clears quarantine from the installed copy after you explicitly run it.

## Prerequisite: Karabiner virtual HID

Dock auto-hide edge pressure requires **Karabiner-DriverKit-VirtualHIDDevice** to be installed and enabled. The TrackPoint daemon remains a normal user process; a small root helper forwards only display-edge pressure through Karabiner's DriverKit virtual mouse.

If you already use Karabiner-Elements, its virtual HID device is normally present. The installer checks for the Karabiner virtual-HID daemon/socket before installing the helper.

## 1. Download the release

Download both files for the desired version from GitHub Releases:

```text
macOS-trackpoint-scroll-<version>.tar.gz
macOS-trackpoint-scroll-<version>.tar.gz.sha256
```

## 2. Verify the checksum

In Terminal, change to the directory containing the download and run:

```sh
shasum -a 256 -c macOS-trackpoint-scroll-<version>.tar.gz.sha256
```

It should report `OK`.

## 3. Extract

```sh
tar -xzf macOS-trackpoint-scroll-<version>.tar.gz
cd macOS-trackpoint-scroll-<version>
```

## 4. Install

Run:

```sh
./install.sh
```

The installer:

- copies the prebuilt app to `~/Applications/macOS-trackpoint-scroll.app`;
- clears `com.apple.quarantine` from that installed copy;
- preserves an existing `~/.config/macOS-trackpoint-scroll.conf`;
- otherwise installs the included default config;
- installs `~/Library/LaunchAgents/io.github.bilinsun02.macos-trackpoint-scroll.plist`;
- prompts once for `sudo` to install the root edge-pressure helper under `/Library/PrivilegedHelperTools`;
- installs its LaunchDaemon under `/Library/LaunchDaemons`;
- starts both services immediately.

It does **not** compile or sign anything on the destination Mac. The main TrackPoint daemon still runs as the logged-in user; only the narrow edge-pressure helper runs as root.

## 5. Grant macOS privacy permissions

Open:

```text
System Settings > Privacy & Security > Input Monitoring
System Settings > Privacy & Security > Accessibility
```

Add or enable:

```text
~/Applications/macOS-trackpoint-scroll.app
```

Input Monitoring is needed for exclusive TrackPoint HID access. Accessibility is needed for the replacement Quartz pointer and scroll events.

After changing either permission, restart the agent:

```sh
launchctl kickstart -k gui/$(id -u)/io.github.bilinsun02.macos-trackpoint-scroll
```

## 6. Verify that it is running

Status:

```sh
launchctl print gui/$(id -u)/io.github.bilinsun02.macos-trackpoint-scroll
```

Logs:

```sh
tail -f "$HOME/Library/Logs/macOS-trackpoint-scroll/stderr.log"
```

Edge-pressure helper status:

```sh
sudo launchctl print system/io.github.bilinsun02.macos-trackpoint-scroll.edge-pressure-helper
```

Edge-pressure helper logs:

```sh
sudo tail -f "/Library/Logs/macOS-trackpoint-scroll/edge-pressure-helper.stderr.log"
```

The user agent and root helper start automatically. No Terminal window needs to remain open.

## Configuration

The per-user configuration file is:

```text
~/.config/macOS-trackpoint-scroll.conf
```

The installer does not overwrite an existing config.

After editing the config, restart the agent:

```sh
launchctl kickstart -k gui/$(id -u)/io.github.bilinsun02.macos-trackpoint-scroll
```

## Updating

Download and extract the newer release, then run its `./install.sh`. The installer replaces the app and LaunchAgent while preserving the existing config.

Because releases are ad-hoc signed rather than Developer-ID signed, macOS may require the Input Monitoring and Accessibility grants to be enabled again after an update.

## Uninstalling

From the extracted release directory:

```sh
./uninstall.sh
```

This removes the LaunchAgent, installed app, root edge-pressure LaunchDaemon, and privileged helper. Configuration and logs are intentionally left in place. Uninstall prompts for `sudo` to remove the privileged helper.

## Gatekeeper / quarantine note

This is deliberately an unsigned-for-distribution, non-notarized release. The app itself carries an ad-hoc code signature, but it does not identify an Apple-registered developer.

The supplied installer clears the browser/GitHub quarantine attribute only from the installed copy. If macOS blocks running `install.sh` itself, invoke it from Terminal as shown above. No Xcode installation or local signing identity is required.
