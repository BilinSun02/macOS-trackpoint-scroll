# macOS TrackPoint Scroll

A macOS port of [`libinput-trackpoint-scroll`](https://github.com/BilinSun02/libinput-trackpoint-scroll), using the same pinned [`trackpoint-scroll-core`](https://github.com/BilinSun02/trackpoint-scroll-core) reconstruction/transfer engine.

Current stable version: **1.1.0**.

Tested adapter:

- vendor `0x5859`
- product `0x0001`
- `xy_3dg12 xy_3dg12 USB RF Adapter`

## v1 architecture

The hardware-validated path uses exclusive HID ownership (`--seize`) together with full virtual-HID forwarding (`--vhid-pointer`). The daemon reads the target mouse through IOKit, preventing the normal macOS mouse stack from consuming its reports. It then:

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

## Prebuilt release installation

A prebuilt release can be installed on a Mac with **no Xcode, compiler toolchain, or Apple code-signing identity**.

Download the `.tar.gz` release, extract it, and run:

```sh
./install.sh
```

The release app is ad-hoc signed during packaging. It is intentionally not Developer-ID signed or notarized, and the installer clears quarantine from the installed copy after you explicitly run it.

See [docs/INSTALLING.md](docs/INSTALLING.md) for the complete fresh-Mac installation, privacy-permission, update, and uninstall procedure.

Maintainers can create the archive with:

```sh
make clean
make dist
```

See [docs/PACKAGING.md](docs/PACKAGING.md) for the complete packaging and release-upload procedure.

## Build

```sh
git clone --recurse-submodules https://github.com/BilinSun02/macOS-trackpoint-scroll.git
cd macOS-trackpoint-scroll
make
```

If needed:

```sh
git submodule update --init --recursive
```

## Install for automatic startup from a source checkout

The source-tree developer installation is:

```sh
make install-user
```

This developer installer requires a persistent code-signing identity. An Apple Development identity created by Xcode is sufficient for local use; verify it with:

```sh
security find-identity -v -p codesigning
```

For a destination Mac without Xcode or an identity, use the **prebuilt release installer** described above instead.

The signed application is installed at:

```text
~/Applications/macOS-trackpoint-scroll.app
```

The per-user LaunchAgent is installed at:

```text
~/Library/LaunchAgents/io.github.bilinsun02.macos-trackpoint-scroll.plist
```

The LaunchAgent runs the app executable with `--seize --edge-pressure-helper --vhid-pointer` in the logged-in GUI session. Installation prompts once for `sudo` to install/start the narrow root virtual-HID bridge; the main TrackPoint daemon itself remains a normal user process.

### macOS privacy permissions

Add/enable `~/Applications/macOS-trackpoint-scroll.app` in both:

```text
System Settings -> Privacy & Security -> Input Monitoring
System Settings -> Privacy & Security -> Accessibility
```

Both are required for the seized architecture:

- **Input Monitoring** permits the app to open/read the target HID device exclusively.
- **Accessibility / CoreGraphics PostEvent access** permits active event taps and replacement Quartz events used by the scroll-rewrite path.

The daemon requests these gates explicitly at startup and logs them separately as
`input-monitoring`, `cg-post-event`, and `ax-trusted`. A manually enabled
Accessibility entry is not sufficient evidence that the running LaunchAgent is
trusted; on the validated macOS 26 test system, the explicit runtime request
produced the effective Accessibility prompt and the active rewrite tap became
available only after that grant.

After changing either permission:

```sh
launchctl kickstart -k gui/$(id -u)/io.github.bilinsun02.macos-trackpoint-scroll
```

Persistent signing plus the stable bundle identifier is used for source-tree developer installs so these grants can survive ordinary rebuild/reinstall cycles. Ad-hoc-signed binary releases may require the grants to be enabled again after an update.

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

The common scroll-profile keys intentionally match `libinput-trackpoint-scroll`.
Set `profile=affine`, `profile=quadratic`, or `profile=hyperbolic`; only the
selected profile's parameter block is used. Profile/timing changes are read at
daemon startup, so restart the LaunchAgent after editing the config:

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

The carrier/rewrite step is intentional. Conventional VHID wheel reports were
measured one-for-one at the CoreGraphics tap, but identical unit reports were
expanded by macOS into strongly rate-dependent point deltas (up to roughly
95 pixels in the observed test). Rewriting those events with the core output
removes that OS-level wheel acceleration while retaining a hardware-origin
scroll event that macOS accepts.

## Manual diagnostic run

For debugging outside launchd while matching the installed architecture:

```sh
./build/macOS-trackpoint-scroll --seize --edge-pressure-helper --vhid-pointer --verbose
```

The privileged helper must already be installed/running for that command.

Remember that a process launched from Terminal can inherit/resolve TCC authorization differently from the LaunchAgent app identity. The installed app/LaunchAgent is the authoritative test for automatic startup.

## Core pin

The core gitlink is pinned to `133df50ea5ce58e71e3fed3240c26999ee689386`.
