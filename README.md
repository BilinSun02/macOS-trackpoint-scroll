# macOS TrackPoint Scroll

A macOS port of [`libinput-trackpoint-scroll`](git@github.com:BilinSun02/libinput-trackpoint-scroll.git), using the same pinned [`trackpoint-scroll-core`](git@github.com:BilinSun02/trackpoint-scroll-core.git) reconstruction/transfer engine.

Current stable version: **1.0.2**.

Tested adapter:

- vendor `0x5859`
- product `0x0001`
- `xy_3dg12 xy_3dg12 USB RF Adapter`

## v1 architecture

The hardware-validated path uses exclusive HID ownership (`--seize`). The daemon reads the target mouse through IOKit, preventing the normal macOS mouse stack from consuming its reports. It then:

- forwards ordinary left/right buttons and pointer motion through Quartz;
- applies an optional raw-HID pointer speed/acceleration curve to ordinary motion;
- converts middle-button motion into continuous pixel scrolling through `trackpoint-scroll-core`;
- keeps scrolling on raw TrackPoint deltas, independent of the pointer curve.

The adapter reports X and Y as separate HID element callbacks. `event_shim.c` keeps a short cache of the most recently posted synthetic cursor position so split-axis reports compose instead of overwriting one another. Synthetic positions are projected onto active display geometry before posting/caching so sustained motion into a screen edge cannot accumulate invisible off-screen overshoot.

Synthetic button events also carry Quartz click-state and event-number bookkeeping, so double-click, triple-click, and click-drag behavior matches ordinary macOS mouse events.

Pointer acceleration is applied before Quartz event creation, using the original `IOHIDValueGetTimeStamp()` timestamps. The default `pointer_acceleration=0` is a linear/no-acceleration path.

## Build

```sh
git clone --recurse-submodules git@github.com:BilinSun02/macOS-trackpoint-scroll.git
cd macOS-trackpoint-scroll
make
```

If needed:

```sh
git submodule update --init --recursive
```

## Install for automatic startup

The supported installation is:

```sh
make install-user
```

The installer requires a persistent code-signing identity. An Apple Development identity created by Xcode is sufficient for local use; verify it with:

```sh
security find-identity -v -p codesigning
```

The signed application is installed at:

```text
~/Applications/macOS-trackpoint-scroll.app
```

The per-user LaunchAgent is installed at:

```text
~/Library/LaunchAgents/io.github.bilinsun02.macos-trackpoint-scroll.plist
```

The LaunchAgent runs the app executable with `--seize` in the logged-in GUI session.

### macOS privacy permissions

Add/enable `~/Applications/macOS-trackpoint-scroll.app` in both:

```text
System Settings -> Privacy & Security -> Input Monitoring
System Settings -> Privacy & Security -> Accessibility
```

Both are required for the seized architecture:

- **Input Monitoring** permits the app to open/read the target HID device exclusively.
- **Accessibility** permits `CGEventPost()` to inject the replacement pointer/button/scroll events.

On the tested Sequoia system, programmatic permission requests did not reliably register or present a prompt, so v1 documents manual authorization instead of depending on prompt behavior.

After changing either permission:

```sh
launchctl kickstart -k gui/$(id -u)/io.github.bilinsun02.macos-trackpoint-scroll
```

Persistent signing plus the stable bundle identifier is used so these grants can survive ordinary rebuild/reinstall cycles.

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

Rebuild/reinstall:

```sh
make clean
make install-user
```

Uninstall:

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
suppress_middle_click=true

pointer_speed=1.0
pointer_acceleration=0.0
pointer_acceleration_velocity=0.10
```

`pointer_speed` is the linear base gain. `pointer_acceleration` adds velocity-dependent gain; zero disables acceleration. `pointer_acceleration_velocity` is the raw speed in counts/ms at which half of the configured extra gain is active.

For nonzero acceleration, the causal gain is based on the preceding HID report:

```text
speed = hypot(dx, dy) / elapsed_ms
response = speed / (speed + pointer_acceleration_velocity)
gain = pointer_speed * (1 + pointer_acceleration * response)
```

The pointer path preserves fractional remainder, resets after long idle periods, and resets when entering/leaving middle-button scrolling. Scrolling itself always receives the unscaled raw HID values.

The TrackPoint scroll direction is intentionally independent of macOS's system-wide Natural Scrolling setting. CLI options are applied after the config file.

## Scroll pipeline

During middle-button scrolling:

1. raw HID X/Y reports are read from the adapter;
2. reports are timestamped and fed to `trackpoint-scroll-core`;
3. sparse reports are causally reconstructed on the 2 ms logical grid;
4. the hyperbolic transfer profile is applied;
5. Quartz continuous pixel scroll events are posted.

Current core/profile defaults mirror the Linux integration:

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

## Manual diagnostic run

For debugging outside launchd:

```sh
./build/macOS-trackpoint-scroll --seize --verbose
```

Remember that a process launched from Terminal can inherit/resolve TCC authorization differently from the LaunchAgent app identity. The installed app/LaunchAgent is the authoritative test for automatic startup.

## Core pin

The core gitlink is pinned to `133df50ea5ce58e71e3fed3240c26999ee689386`.
