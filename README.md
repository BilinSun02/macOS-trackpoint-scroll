# macOS TrackPoint Scroll

A macOS port of [`libinput-trackpoint-scroll`](git@github.com:BilinSun02/libinput-trackpoint-scroll.git), using the same pinned [`trackpoint-scroll-core`](git@github.com:BilinSun02/trackpoint-scroll-core.git) reconstruction/transfer engine.

Tested adapter:

- vendor `0x5859`
- product `0x0001`
- `xy_3dg12 xy_3dg12 USB RF Adapter`

The adapter being classified by macOS as a generic mouse is not a protocol problem: the program matches it directly through IOKit by HID identity.

## Status

The hardware-validated path is exclusive HID ownership:

```sh
sudo ./build/macOS-trackpoint-scroll --seize
```

`--seize` prevents the generic macOS mouse stack from consuming TrackPoint reports. Ordinary TrackPoint movement and buttons are forwarded back through Quartz; while middle is held, the same raw motion is sent only to the scroll engine. This avoids simultaneous cursor motion during scrolling and resumes pointer movement immediately on release.

The non-seized path remains available for experimentation, but attempts to suppress the underlying generic-mouse cursor movement at the Quartz level were not reliable on the tested adapter.

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

For diagnostics:

```sh
sudo ./build/macOS-trackpoint-scroll --seize --verbose
```

Input Monitoring / Accessibility permission may be required for keyboard modifier handling and synthesized events.

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

## Shift axis lock and Scroll Lock

The modifier ordering mirrors the Linux implementation.

- Free mode is the default: both scroll axes remain available.
- Shift already held before middle-down remains visible to applications and does not change the gesture.
- A fresh left- or right-Shift press after middle-down toggles the current gesture between free and locked mode. That Shift press and its matching release are consumed.
- Locked mode chooses the dominant axis from post-lock scroll output and keeps the gesture on that axis until the mode changes or the middle button is released.
- A mode change discards pending pre-switch reconstruction with `TPSC_RESTART_BYPASS_STARTUP`; only post-switch motion contributes afterward.
- A fresh Scroll Lock press toggles the default mode for subsequent gestures. It remains visible to normal software and auto-repeat does not repeatedly toggle the custom state.
- The Scroll Lock default is latched at middle-down; pressing Scroll Lock during an active gesture affects only later gestures.

macOS normally exposes a PC keyboard's physical Scroll Lock key as F14 (virtual key code `0x6b`), which is what this port watches.

The resulting state table matches the Linux behavior:

```text
custom Scroll Lock state off:
  middle + motion       -> free
  middle then Shift     -> locked

custom Scroll Lock state on:
  middle + motion       -> locked
  middle then Shift     -> free
```

The Linux locked path delegates buildup/direction/axis locking to libinput's `evdev_post_scroll()`. macOS has no equivalent API, so this port implements a local dominant-axis latch after the shared reconstruction/profile stage.

## Scroll pipeline

During middle-button scrolling:

1. raw HID X/Y reports are read from the adapter;
2. reports are timestamped and fed to `trackpoint-scroll-core`;
3. sparse reports are causally reconstructed on the 2 ms logical grid;
4. the hyperbolic transfer profile is applied;
5. modifier mode filtering is applied;
6. Quartz continuous pixel scroll events are posted.

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

## Core pin

The core gitlink is pinned to `133df50ea5ce58e71e3fed3240c26999ee689386`, matching the core-backed Linux candidate.
