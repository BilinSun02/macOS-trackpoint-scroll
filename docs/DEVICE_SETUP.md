# Device setup on macOS

## Tested device

```text
name:    xy_3dg12 xy_3dg12 USB RF Adapter
vendor:  0x5859
product: 0x0001
usage:   Generic Desktop / Mouse
```

Relevant HID controls are relative X/Y plus left, right, and middle buttons.

## Generic-mouse classification

Linux needed a udev pointing-stick classification because libinput selected TrackPoint policy from that property. This macOS port matches the device directly by HID vendor ID, product ID, and Generic Desktop / Mouse usage, so the generic-mouse classification is not a blocker.

The tested adapter's report descriptor exposes ordinary relative mouse axes/buttons. The program therefore treats device identity and TrackPoint policy as application concerns rather than relying on macOS to classify the hardware specially.

## Supported v1 mode: exclusive seize

The validated v1 path opens the matching HID device with `kIOHIDOptionsTypeSeizeDevice`.

Consequences:

- the normal macOS mouse consumer no longer receives the adapter's events;
- the daemon forwards ordinary pointer motion and left/right buttons through Quartz;
- middle+motion is consumed as TrackPoint scrolling;
- the target adapter is isolated from unrelated pointing devices.

The LaunchAgent runs as the logged-in user; root/sudo is not part of the supported installation.

## Permissions

The installed signed application requires both of these manual grants:

```text
System Settings -> Privacy & Security -> Input Monitoring
System Settings -> Privacy & Security -> Accessibility
```

Add/enable:

```text
~/Applications/macOS-trackpoint-scroll.app
```

Input Monitoring is required for the exclusive IOHID open. Without it, `IOHIDManagerOpen` returns `kIOReturnNotPermitted` (`0xe00002e2`).

Accessibility is required for synthetic Quartz event posting. Without it, the daemon can successfully seize the TrackPoint while its replacement pointer events are not accepted, leaving the cursor apparently immobile.

On the tested Sequoia system, automatic permission requests were not reliable enough to use as the installation contract. After granting permissions, restart the LaunchAgent:

```sh
launchctl kickstart -k gui/$(id -u)/io.github.bilinsun02.macos-trackpoint-scroll
```

## Verify device discovery

The installed daemon logs to:

```text
~/Library/Logs/macOS-trackpoint-scroll/stderr.log
```

A healthy startup includes lines similar to:

```text
trackpoint: running for vid=5859 pid=0001, scale=8, direction=traditional [exclusive seize mode]
trackpoint: matched HID device xy_3dg12 USB RF Adapter (vid=5859 pid=0001) [seized]
trackpoint: raw HID pointer curve speed=1 acceleration=0 velocity=0.1 counts/ms
```

If the HID open fails with `0xe00002e2`, check Input Monitoring. If the device matches/seizes but pointer and scroll injection are ineffective, check Accessibility.

## Pointer forwarding

The adapter reports X and Y as separate HID callbacks. Quartz event posting is asynchronous, so the second axis can otherwise query an old cursor position and overwrite the first. The event shim caches the most recently posted synthetic position for 50 ms, allowing split-axis reports to compose.

Optional pointer speed/acceleration is applied to the raw HID values before pointer events are created. It uses the HID timestamps, not synthetic Quartz timestamps. Middle-button scrolling bypasses this pointer curve and feeds raw deltas to the shared scroll core.

## Sparse-report behavior

No macOS-side smoothing is added before the shared scroll core. Low-force reports from this adapter can be separated by tens to hundreds of milliseconds, while higher force can approach roughly 10 ms. Raw HID timestamps are preserved so the core's startup coalescing, interval estimation, causal fixed-time reconstruction, and nonlinear transfer function retain their intended behavior.
