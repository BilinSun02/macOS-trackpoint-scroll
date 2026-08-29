# macOS TrackPoint Scroll

A macOS port of [`libinput-trackpoint-scroll`](git@github.com:BilinSun02/libinput-trackpoint-scroll.git), using the same pinned [`trackpoint-scroll-core`](git@github.com:BilinSun02/trackpoint-scroll-core.git) reconstruction/transfer engine.

Tested adapter:

- vendor `0x5859`
- product `0x0001`
- `xy_3dg12 xy_3dg12 USB RF Adapter`

The adapter being classified by macOS as a generic mouse is not a protocol problem: the program matches it directly through IOKit by HID identity.

## Stable path

The hardware-validated implementation uses exclusive HID ownership (`--seize`). This prevents the generic macOS mouse stack from consuming TrackPoint reports. Ordinary TrackPoint movement and buttons are forwarded back through Quartz; while middle is held, the same raw motion is sent only to the scroll engine.

The unfinished Shift/Scroll-Lock experiment is intentionally **not part of the stable build**.

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

Install the built binary as a per-user LaunchAgent:

```sh
make install-user
```

This copies the executable to:

```text
~/Library/Application Support/macOS-trackpoint-scroll/macOS-trackpoint-scroll
```

and installs:

```text
~/Library/LaunchAgents/io.github.bilinsun02.macos-trackpoint-scroll.plist
```

The LaunchAgent runs the installed executable with `--seize` and the explicit config path. It starts at login in the user's GUI/Aqua session, which is important because synthesized Quartz events need access to that session.

Logs are written to:

```text
~/Library/Logs/macOS-trackpoint-scroll/stdout.log
~/Library/Logs/macOS-trackpoint-scroll/stderr.log
```

Useful diagnostics:

```sh
launchctl print gui/$(id -u)/io.github.bilinsun02.macos-trackpoint-scroll
tail -f ~/Library/Logs/macOS-trackpoint-scroll/stderr.log
```

To rebuild and replace the installed copy:

```sh
make clean
make install-user
```

To uninstall startup integration:

```sh
make uninstall-user
```

The uninstaller intentionally leaves the config and logs in place.

If macOS requests Input Monitoring or Accessibility permission, grant it to the installed executable at the stable path above. Replacing the binary with a new build can cause macOS to request privacy approval again.

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
```

The TrackPoint direction is intentionally independent of macOS's system-wide Natural Scrolling setting. CLI options are applied after the config file.

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

macOS scroll units are not identical to libinput scroll units, so `scroll_scale` / `--scroll-scale` remains exposed for calibration.

## Seized pointer forwarding

The adapter reports X and Y as separate HID element callbacks. Quartz posting is asynchronous enough that querying cursor position independently for each axis can make one axis overwrite the other. `event_shim.c` therefore retains the most recently posted synthetic pointer position for a short split-axis window so the two reports compose correctly.

Because ordinary pointer motion is synthesized in `--seize` mode, it does not pass through macOS's native hardware-pointer acceleration path exactly as an unseized mouse would.

## Manual diagnostic run

Manual foreground execution remains useful for debugging only:

```sh
./build/macOS-trackpoint-scroll --seize --verbose
```

If exclusive HID open is denied for an ordinary user account on a particular macOS installation, the log will show the `IOHIDManagerOpen` error. In that case the startup architecture will need a small privileged HID helper paired with the per-user GUI agent rather than moving the whole program into a root LaunchDaemon.

## Core pin

The core gitlink is pinned to `133df50ea5ce58e71e3fed3240c26999ee689386`, matching the core-backed Linux candidate.
