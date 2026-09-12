# macOS TrackPoint Scroll

A macOS port of [`libinput-trackpoint-scroll`](https://github.com/BilinSun02/libinput-trackpoint-scroll), using the same pinned [`trackpoint-scroll-core`](https://github.com/BilinSun02/trackpoint-scroll-core) reconstruction/transfer engine.

Current stable version: **1.2.1**.

Tested adapter:

- vendor `0x5859`
- product `0x0001`
- `xy_3dg12 xy_3dg12 USB RF Adapter`

## Installation choices

### Build and install from source

This is the recommended path when no official prebuilt release is available.

A paid Apple Developer Program membership is **not required** to build and run the software on your own Mac. The source installer needs a persistent local code-signing identity so macOS can associate Input Monitoring and Accessibility/TCC grants with a stable app identity. An **Apple Development** identity created by Xcode for your Apple Account is sufficient for this local workflow.

The complete clean-Mac walkthrough is in [docs/BUILDING_FROM_SOURCE.md](docs/BUILDING_FROM_SOURCE.md). It covers:

1. installing Xcode and creating an Apple Development identity;
2. installing/enabling Karabiner-Elements and its DriverKit virtual HID device;
3. cloning the repository and pinned submodule;
4. building and installing with `make install-user`;
5. granting macOS privacy permissions;
6. verifying the user daemon and root virtual-HID helper;
7. updating, uninstalling, and optionally creating your own archive.

The short version, after the signing identity and Karabiner virtual HID are ready, is:

```sh
git clone --recurse-submodules https://github.com/BilinSun02/macOS-trackpoint-scroll.git
cd macOS-trackpoint-scroll
make install-user
```

If multiple signing identities are installed, select one explicitly:

```sh
security find-identity -v -p codesigning
MACOS_TRACKPOINT_CODESIGN_IDENTITY="<40-character-identity-hash>" make install-user
```

Do not casually switch identities between updates; keeping the same persistent identity helps preserve the app's effective macOS privacy identity across rebuilds.

### Prebuilt release installation

A prebuilt release, when one is published, can be installed on a destination Mac with **no Xcode, compiler toolchain, or local signing identity**. Download the `.tar.gz` release, extract it, and run:

```sh
./install.sh
```

See [docs/INSTALLING.md](docs/INSTALLING.md) for checksum verification, installation, privacy permissions, updating, and uninstalling.

An Apple Development-signed archive is suitable for controlled development/testing, but the signing certificate identity is inspectable by recipients and it is not the normal public-distribution path. Broad public distribution should use **Developer ID Application** signing and notarization. See [docs/PACKAGING.md](docs/PACKAGING.md).

## Architecture

The hardware-validated path uses exclusive HID ownership (`--seize`) together with the privileged virtual-HID helper (`--edge-pressure-helper`). The daemon reads the target mouse through IOKit, preventing the normal macOS mouse stack from consuming its reports. It then:

- applies the optional raw-HID pointer speed/acceleration curve to ordinary motion;
- forwards ordinary pointer motion and button state through Karabiner's DriverKit virtual mouse;
- converts middle-button motion into continuous pixel scrolling through `trackpoint-scroll-core`;
- keeps scrolling on raw TrackPoint deltas, independent of the pointer curve;
- sends a unit virtual-HID wheel report as a hardware-class carrier and rewrites the resulting CoreGraphics scroll event to the exact core output.

Using one relative virtual mouse for ordinary pointer motion also gives macOS genuine HID-class display-edge pressure, so Dock auto-hide reveal no longer needs the earlier Quartz absolute-position/edge-latch workaround in the installed path.

### Virtual-HID bridge

A small root LaunchDaemon owns the root-only **Karabiner-DriverKit-VirtualHIDDevice** connection. The logged-in user's daemon sends only compact relative pointer/button/wheel reports to it over a user-owned `0600` Unix socket. The helper does not interpret gestures or read the physical TrackPoint; it is only the privileged transport bridge to the DriverKit virtual mouse.

This requires Karabiner's virtual HID device/daemon to be installed and enabled. The helper is installed at:

```text
/Library/PrivilegedHelperTools/io.github.bilinsun02.macos-trackpoint-scroll.edge-pressure-helper
```

and managed by:

```text
/Library/LaunchDaemons/io.github.bilinsun02.macos-trackpoint-scroll.edge-pressure-helper.plist
```

Pointer acceleration is applied before the virtual-HID report is emitted, using the original `IOHIDValueGetTimeStamp()` timestamps. The default `pointer_acceleration=0` is a linear/no-acceleration path.

For durable architecture rules, known macOS input-stack behavior, symptom-to-cause guidance, and debugging practices, see [docs/ENGINEERING_NOTES.md](docs/ENGINEERING_NOTES.md).

## Build only

If you only want to compile without installing:

```sh
git clone --recurse-submodules https://github.com/BilinSun02/macOS-trackpoint-scroll.git
cd macOS-trackpoint-scroll
make
```

If needed:

```sh
git submodule update --init --recursive
```

A successful build produces:

```text
build/macOS-trackpoint-scroll
build/macOS-trackpoint-scroll-edge-pressure-helper
```

## Installed source-build layout

`make install-user` installs the signed application at:

```text
~/Applications/macOS-trackpoint-scroll.app
```

and the per-user LaunchAgent at:

```text
~/Library/LaunchAgents/io.github.bilinsun02.macos-trackpoint-scroll.plist
```

The LaunchAgent runs the app executable with `--seize --edge-pressure-helper` in the logged-in GUI session. Installation prompts once for `sudo` to install/start the narrow root virtual-HID bridge; the main TrackPoint daemon itself remains a normal user process.

### macOS privacy permissions

Add/enable `~/Applications/macOS-trackpoint-scroll.app` in both:

```text
System Settings -> Privacy & Security -> Input Monitoring
System Settings -> Privacy & Security -> Accessibility
```

Both are required for the seized architecture:

- **Input Monitoring** permits the app to open/read the target HID device exclusively.
- **Accessibility / CoreGraphics PostEvent access** permits active event taps and replacement Quartz events used by the scroll-rewrite path.

The daemon requests these gates explicitly at startup and logs them separately as `input-monitoring`, `cg-post-event`, and `ax-trusted`. A manually enabled Accessibility entry is not sufficient evidence that the running LaunchAgent is trusted; use the runtime log as the authoritative check.

After changing either permission:

```sh
launchctl kickstart -k gui/$(id -u)/io.github.bilinsun02.macos-trackpoint-scroll
```

Logs:

```text
~/Library/Logs/macOS-trackpoint-scroll/stdout.log
~/Library/Logs/macOS-trackpoint-scroll/stderr.log
```

Useful diagnostics:

```sh
launchctl print gui/$(id -u)/io.github.bilinsun02.macos-trackpoint-scroll
tail -f ~/Library/Logs/macOS-trackpoint-scroll/stderr.log
```

Rebuild/reinstall from source:

```sh
make clean
make install-user
```

Uninstall from source:

```sh
make uninstall-user
```

The uninstaller leaves config and logs in place.

## Configuration

Default path:

```text
~/.config/macOS-trackpoint-scroll.conf
```

Example:

```ini
natural_scroll=false
scroll_scale=8.0

profile=hyperbolic
clamp_negative_output=false
suppress_middle_click=true

first_step_distance=0.4
first_step_axis_merge_ms=70.0
first_step_max_reports=-1
idle_reset_ms=333.3

affine_k=0.4
affine_b=0.0

quadratic_a=0.16
quadratic_h=0.0
quadratic_k=0.025

hyperbolic_a=0.75
hyperbolic_u=1.6
hyperbolic_k=-1.175

pointer_speed=1.0
pointer_acceleration=0.0
pointer_acceleration_velocity=0.10
```

The common scroll-profile keys intentionally match `libinput-trackpoint-scroll`. Set `profile=affine`, `profile=quadratic`, or `profile=hyperbolic`; only the selected profile's parameter block is used. Profile/timing changes are read at daemon startup, so restart the LaunchAgent after editing the config:

```sh
launchctl kickstart -k gui/$(id -u)/io.github.bilinsun02.macos-trackpoint-scroll
```

`pointer_speed` is the linear base gain. `pointer_acceleration` adds velocity-dependent gain; zero disables acceleration. `pointer_acceleration_velocity` is the raw speed in counts/ms at which half of the configured extra gain is active.

For nonzero acceleration, the causal gain is based on the preceding HID report:

```text
speed = hypot(dx, dy) / elapsed_ms
response = speed / (speed + pointer_acceleration_velocity)
gain = pointer_speed * (1 + pointer_acceleration * response)
```

The pointer path preserves fractional remainder, resets after long idle periods, and resets when entering/leaving middle-button scrolling. Scrolling itself always receives the unscaled raw HID values.

`rebound_filter` is retained for the older Quartz compatibility path, but the full virtual-HID installed path does not currently feed that retrospective Quartz correction layer. Leave it disabled in the installed full-VHID configuration.

The TrackPoint scroll direction is intentionally independent of macOS's system-wide Natural Scrolling setting. CLI options are applied after the config file.

## Scroll pipeline

During middle-button scrolling:

1. raw HID X/Y reports are read from the adapter;
2. reports are timestamped and fed to `trackpoint-scroll-core`;
3. sparse reports are causally reconstructed on the 2 ms logical grid;
4. the configured affine, quadratic, or hyperbolic transfer profile is applied;
5. in full virtual-HID pointer mode, a unit Karabiner VHID wheel report acts as a hardware-class carrier;
6. an active CoreGraphics event tap replaces macOS's accelerated wheel magnitude with the exact floating-point core output before applications receive it.

Current core/profile defaults mirror the Linux integration, and these values are configurable on macOS:

```text
profile=hyperbolic
clamp_negative_output=false
first_step_distance=0.4
first_step_axis_merge_ms=70.0
first_step_max_reports=-1
idle_reset_ms=333.3
hyperbolic_a=0.75
hyperbolic_u=1.6
hyperbolic_k=-1.175
```

macOS scroll units are not identical to libinput scroll units, so `scroll_scale` remains exposed for calibration.

The carrier/rewrite step is intentional. Conventional VHID wheel reports were measured one-for-one at the CoreGraphics tap, but identical unit reports were expanded by macOS into strongly rate-dependent point deltas (up to roughly 95 pixels in the observed test). Rewriting those events with the core output removes that OS-level wheel acceleration while retaining a hardware-origin scroll event that macOS accepts.

## Manual diagnostic run

For debugging outside launchd while matching the installed architecture:

```sh
./build/macOS-trackpoint-scroll --seize --edge-pressure-helper --verbose
```

The privileged helper must already be installed/running for that command.

Remember that a process launched from Terminal can inherit/resolve TCC authorization differently from the LaunchAgent app identity. The installed app/LaunchAgent is the authoritative test for automatic startup.

## Core pin

The core gitlink is pinned to `133df50ea5ce58e71e3fed3240c26999ee689386`.
