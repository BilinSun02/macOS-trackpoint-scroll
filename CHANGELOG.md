# Changelog

## 1.2.0 - 2026-09-10

Hardware-validated virtual-HID pointer and exact-scroll release.

### Changed

- Use exclusive TrackPoint ownership plus a relative Karabiner DriverKit virtual mouse for ordinary pointer motion and left/right button state.
- Use a unit virtual-HID wheel event as the hardware-class scroll carrier, then rewrite its CoreGraphics delta/point/fixed magnitudes to the exact floating-point output produced by `trackpoint-scroll-core`.
- Keep the privileged helper as a narrow pointer/button/wheel bridge with heartbeat, reconnect/reinitialize, UID-restricted IPC, and defensive all-buttons-up cleanup.
- Expose the shared core's affine, quadratic, and hyperbolic profiles and reconstruction timing parameters through the macOS config.
- Request and report Input Monitoring, CoreGraphics PostEvent access, and Accessibility trust as separate runtime gates.

### Fixed

- Eliminate the small-motion deadzone caused by reconstructing a seized relative pointer through absolute Quartz cursor events.
- Eliminate edge-pressure axis locking and delayed inward reversal in the canonical path.
- Restore repeatable Dock auto-hide edge pressure, including after virtual-HID idle periods.
- Remove macOS's steep rate-dependent wheel amplification from TrackPoint scrolling by rewriting the carrier event to the core's intended magnitude.
- Preserve configured natural-scroll direction on the virtual-HID path.
- Prevent a user-daemon disconnect from leaving virtual mouse buttons logically held.

### Packaging

- Require a stable Apple-issued signing identity for normal `make dist` packaging.
- Keep ad-hoc packaging only as an explicit diagnostic/CI opt-in because rebuilt ad-hoc apps do not provide stable TCC identity.

## 1.0.2 - 2026-08-29

Hardware-validated synthetic click-state bugfix.

### Fixed

- Populate Quartz mouse click-state fields so TrackPoint double-clicks and triple-clicks are recognized normally.
- Keep matching mouse-down/mouse-up events on the same synthetic event number.
- Respect the user's double-click timing preference with a safe fallback and positional slop.
- Carry click state through drags and invalidate a click sequence after an actual drag.

## 1.0.1 - 2026-08-29

Hardware-validated cursor edge bugfix.

### Fixed

- Clamp synthetic pointer positions to the active display geometry before posting and caching them.
- Prevent invisible off-screen cursor overshoot from accumulating while the TrackPoint is pushed against a screen edge, so reversing direction moves the cursor inward immediately.
- Preserve the split-axis X/Y composition cache while keeping its cached position physically realizable.

## 1.0.0 - 2026-08-29

First hardware-validated stable release.

### Added

- Exclusive IOHID seize mode for the tested `0x5859:0x0001` TrackPoint adapter.
- Ordinary pointer and left/right button reinjection through Quartz.
- Middle-button TrackPoint scrolling backed by the pinned `trackpoint-scroll-core` engine.
- Continuous pixel scroll events with fixed-point and point-delta fields.
- Split-axis synthetic cursor-position cache for adapters that emit X/Y as separate HID callbacks.
- Raw-HID pointer sensitivity and optional velocity-dependent acceleration using `IOHIDValueGetTimeStamp()`.
- Per-user LaunchAgent installation as a persistently signed background app.

### Installation contract

- Requires a persistent local code-signing identity (Apple Development is supported).
- Requires manual Input Monitoring authorization for exclusive HID access.
- Requires manual Accessibility authorization for replacement Quartz event posting.

### Defaults

```ini
natural_scroll=false
scroll_scale=8.0
suppress_middle_click=true
pointer_speed=1.0
pointer_acceleration=0.0
pointer_acceleration_velocity=0.10
```

### Notes

- Pointer acceleration is disabled by default.
- Middle-button scrolling always consumes raw, unscaled HID deltas.
- Shift/Scroll-Lock mode switching is intentionally not part of 1.0.0; it will be developed after the v1 baseline.
