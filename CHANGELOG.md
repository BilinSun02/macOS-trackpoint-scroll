# Changelog

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
