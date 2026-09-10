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
- the daemon forwards ordinary pointer motion and button state through Karabiner's DriverKit virtual mouse;
- middle+motion is consumed as TrackPoint scrolling;
- middle-button scroll output uses a virtual-HID wheel carrier plus an active CoreGraphics rewrite tap;
- the target adapter is isolated from unrelated pointing devices.

The main LaunchAgent runs as the logged-in user. A narrow root helper is installed with `sudo` because Karabiner's virtual-HID service exposes its pointing-device connection through a root-only socket; the helper only bridges compact pointer/button/wheel reports from the user daemon.

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

Accessibility/CoreGraphics PostEvent access is required for the active scroll rewrite tap. Without it, the daemon can seize the TrackPoint and forward pointer motion through virtual HID, but the validated exact-magnitude scroll path cannot arm.

The daemon requests Input Monitoring, CoreGraphics PostEvent access, and Accessibility trust at startup. Permission prompts are asynchronous, so after granting them restart the LaunchAgent:

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
trackpoint: privacy input-monitoring=granted cg-post-event=granted ax-trusted=yes
trackpoint: active scroll rewrite tap created at HID tap
trackpoint: VHID scroll carrier rewrite enabled (HID tap)
trackpoint: running for vid=5859 pid=0001, scale=8, direction=traditional, profile=hyperbolic [exclusive seize + full virtual-HID pointer]
trackpoint: matched HID device xy_3dg12 USB RF Adapter (vid=5859 pid=0001) [seized]
trackpoint: raw HID pointer curve speed=1 acceleration=0 velocity=0.1 counts/ms
```

If the HID open fails with `0xe00002e2`, check Input Monitoring. If the active scroll rewrite tap cannot be created, check the PostEvent/Accessibility grants.

## Pointer forwarding

Optional pointer speed/acceleration is applied to the raw HID values before relative virtual-HID reports are sent. It uses the HID timestamps, not synthetic CoreGraphics timestamps. Middle-button scrolling bypasses this pointer curve and feeds raw deltas to the shared scroll core.

The absolute-Quartz split-axis cache and display projection remain only in the non-VHID compatibility path; the installed configuration does not use them.

## Sparse-report behavior

No macOS-side smoothing is added before the shared scroll core. Low-force reports from this adapter can be separated by tens to hundreds of milliseconds, while higher force can approach roughly 10 ms. Raw HID timestamps are preserved so the core's startup coalescing, interval estimation, causal fixed-time reconstruction, and nonlinear transfer function retain their intended behavior.
