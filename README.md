# macOS TrackPoint Scroll

A macOS port of the TrackPoint middle-button scrolling behavior from [`libinput-trackpoint-scroll`](git@github.com:BilinSun02/libinput-trackpoint-scroll.git), using the same pinned [`trackpoint-scroll-core`](git@github.com:BilinSun02/trackpoint-scroll-core.git) reconstruction/transfer engine.

The tested adapter from the Linux project is matched directly by USB/HID identity:

- vendor: `0x5859`
- product: `0x0001`
- name: `xy_3dg12 xy_3dg12 USB RF Adapter`

macOS may initially expose it as an ordinary generic mouse. That is **not a blocker** here: the program discovers the physical HID device directly with IOKit instead of depending on an operating-system "pointing stick" classification. In normal mode macOS still owns the ordinary mouse path; raw TrackPoint deltas are sent through `trackpoint-scroll-core` and re-emitted as continuous scroll events.

A Quartz event tap is used to suppress application-visible middle-click/drag events, but that alone cannot stop WindowServer from moving the system cursor: filtering a mouse-move event prevents applications from receiving it after the hardware movement has already affected cursor position. Normal mode therefore pins the cursor to its gesture-start position while the target device's raw middle button is held. The pinning helper is device-specific and matches the same USB/HID adapter rather than reacting to arbitrary mice.

An optional `--seize` mode is also provided. It opens the matching HID device with `kIOHIDOptionsTypeSeizeDevice`, preventing the generic mouse stack from consuming it. In seize mode this program must forward ordinary pointer motion/buttons itself, so normal mode is preferred unless a host-specific issue requires exclusive ownership.

## Status

Hardware validation on the target adapter confirms that raw input and synthesized scrolling work on macOS. Cursor pinning during the middle-button gesture is the current normal-mode strategy for preventing simultaneous pointer motion.

## Build

Requires macOS, Xcode Command Line Tools, and Git.

```sh
git clone --recurse-submodules git@github.com:BilinSun02/macOS-trackpoint-scroll.git
cd macOS-trackpoint-scroll
make
```

If the repository was cloned without submodules:

```sh
git submodule update --init --recursive
```

Run:

```sh
./build/macOS-trackpoint-scroll
```

The first launch will require **Input Monitoring** / **Accessibility** permission for the terminal or binary, because Quartz event taps and synthetic scroll events are used.

For diagnostics:

```sh
./build/macOS-trackpoint-scroll --verbose
```

Exclusive fallback:

```sh
sudo ./build/macOS-trackpoint-scroll --seize
```

See [`docs/DEVICE_SETUP.md`](docs/DEVICE_SETUP.md) for the generic-mouse classification discussion and troubleshooting.

## Behavior

While the target device's middle button is held:

1. raw HID `X`/`Y` reports are read from the adapter;
2. reports are fed to `trackpoint-scroll-core` with their HID timestamps;
3. the core reconstructs sparse low-force reports and applies the hyperbolic transfer profile used by the Linux integration;
4. a 2 ms logical timer drains core output;
5. Quartz continuous pixel-scroll events are posted;
6. the cursor is pinned to its gesture-start position so the same physical TrackPoint motion does not move the pointer.

The default core/profile settings mirror the Linux configuration:

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

macOS scroll units are not identical to libinput scroll units, so `--scroll-scale` is intentionally exposed for host calibration.

## Caveats

Normal mode deliberately leaves the generic macOS mouse path active outside the scroll gesture so ordinary pointer acceleration and button handling stay native. During a middle-button scroll gesture, cursor position is repeatedly restored with Core Graphics while the raw HID reports continue feeding the scroll engine. There may be a very small visual jitter if WindowServer advances the cursor between pinning ticks; if that proves noticeable, the next escalation is dynamic/exclusive HID ownership rather than further Quartz event filtering.

In `--seize` mode, system mouse acceleration is bypassed for forwarded pointer motion because Quartz receives synthesized cursor movement rather than the original HID event. This mode exists primarily as a robust ownership fallback.

## Origin / core pin

The core gitlink is pinned to `133df50ea5ce58e71e3fed3240c26999ee689386`, the same core-backed candidate recorded by `libinput-trackpoint-scroll` 0.1.0 work.
