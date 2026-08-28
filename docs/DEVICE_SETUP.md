# Device setup on macOS

## Tested Linux-side identity

The source project records this adapter:

```text
name:    xy_3dg12 xy_3dg12 USB RF Adapter
bus:     0x0003
vendor:  0x5859
product: 0x0001
version: 0x0110
```

Relevant HID controls are relative X/Y plus left, right, and middle buttons.

## The generic-mouse classification is not a blocker

Linux needed a udev property (`ID_INPUT_POINTINGSTICK=1`) because libinput selected TrackPoint-specific policy from that classification. macOS has no equivalent requirement in this port.

This program matches the physical HID device directly by vendor ID, product ID, and Generic Desktop / Mouse HID usage. It therefore does not need the operating system to call the adapter a TrackPoint or pointing stick.

In normal mode the generic-mouse path is intentionally left in place for ordinary cursor movement. When the target adapter's raw middle-button state starts a scroll gesture, a Quartz event tap suppresses native mouse-movement events while the raw IOKit X/Y reports are processed by `trackpoint-scroll-core` and emitted as scroll events.

That means the initial generic-mouse classification is useful rather than harmful outside the gesture: macOS continues to apply its normal pointer behavior when you are not scrolling.

## Exclusive fallback (`--seize`)

If a particular macOS release, USB bridge, or desktop utility produces duplicate events, use:

```sh
sudo ./build/macOS-trackpoint-scroll --seize
```

The HID manager is then opened with `kIOHIDOptionsTypeSeizeDevice`. The matching device is exclusively owned by this process, so the normal generic-mouse consumer no longer receives its HID events. The program forwards left/right buttons and ordinary relative motion itself, while middle+motion is converted to scrolling.

Tradeoff: synthesized pointer movement does not pass through exactly the same native mouse-acceleration pipeline as the original hardware event. Prefer normal mode unless exclusive ownership is actually required.

## Permissions

Normal mode needs permission to install a Quartz event tap and post synthetic scroll events. Depending on macOS version, grant the terminal/application running the binary access under:

```text
System Settings -> Privacy & Security -> Input Monitoring
System Settings -> Privacy & Security -> Accessibility
```

After changing permissions, quit and relaunch the process.

`--seize` can additionally require elevated privileges for exclusive HID access.

## Verify device discovery

Build and run with diagnostics:

```sh
make
./build/macOS-trackpoint-scroll --verbose
```

On a successful match, startup output should include a line similar to:

```text
trackpoint: matched HID device xy_3dg12 xy_3dg12 USB RF Adapter (vid=5859 pid=0001)
```

If it does not match, inspect the adapter in System Information -> USB, then override IDs if needed:

```sh
./build/macOS-trackpoint-scroll --vendor 5859 --product 0001 --verbose
```

The command-line values are hexadecimal.

## Multiple pointing devices

Quartz mouse events do not expose a stable USB vendor/product identity suitable for event-tap filtering. In normal mode, raw gesture ownership is device-specific on the IOKit side, but motion suppression at the Quartz layer applies while the TrackPoint's middle button is held. Therefore movement from another mouse at the exact same time can also be suppressed.

If that matters, use `--seize`, which provides strict per-device isolation by preventing the target adapter's native mouse events from being generated in the first place.

## Sparse-report behavior

No macOS-side smoothing is added before the shared core. The Linux hardware trace showed low-force reports separated by roughly tens to hundreds of milliseconds (with a recorded median near 129 ms and ordinary gaps reaching roughly 440 ms), while higher force can approach ~10 ms. Raw HID timestamps are preserved and passed directly to the core so its startup coalescing, interval estimation, causal fixed-time reconstruction, and nonlinear transfer function retain their intended behavior.
