# Engineering notes

This file records durable implementation and debugging knowledge for the macOS input path. It is intentionally not a development diary.

## Canonical input architecture

```text
physical TrackPoint
    -> exclusive IOHID seize in the user daemon
    -> raw relative reports

ordinary pointer/buttons
    -> narrow root bridge
    -> Karabiner DriverKit virtual pointing device
    -> macOS HID pointer stack

middle-button motion
    -> trackpoint-scroll-core
    -> exact floating-point target is queued
    -> tiny +/-1 virtual-HID wheel carrier
    -> macOS creates a hardware-origin scroll event
    -> active CoreGraphics event tap rewrites its magnitudes
    -> application receives the exact core output
```

Keep physical-device reading and gesture policy in the normal user process. The root process should remain a narrow transport bridge.

## Keep relative pointing relative

The physical adapter reports relative X/Y. Do not make absolute Quartz cursor reconstruction the canonical backend for seized TrackPoint motion.

On the validated macOS path, raw deltas could arrive and be posted while gentle movement remained visibly stuck. Changing only the output transport from synthetic absolute Quartz events to relative virtual HID removed that deadzone.

Absolute reconstruction also creates edge-state hazards. If a compatibility path caches a requested cursor position beyond the visible display, outward motion can accumulate invisible overshoot; reversing inward then appears delayed while the cached distance is worked off. Any absolute compatibility implementation must project/clamp the position before both posting and caching it.

Some adapters deliver X and Y as separate HID callbacks. Do not infer a missing axis from cursor feel alone; instrument raw per-axis input and the immediately following output boundary first.

### Symptom guide

- **Gentle pressure produces raw deltas but no visible motion:** suspect the synthetic pointer transport before changing the TrackPoint curve.
- **One axis disappears near an edge:** inspect edge/handoff code for suppression of the orthogonal component.
- **Inward reversal requires extra motion:** inspect cached off-screen coordinates or accumulated edge state.

## Dock edge pressure is HID behavior

Synthetic Quartz mouse movement is not equivalent to genuine relative HID movement at display edges. Dock auto-hide pressure became reliable when the TrackPoint was represented by a relative virtual HID pointing device.

Do not emulate Dock pressure with synthetic overdrive or a latch that suppresses normal motion. A latch that suppresses an entire Quartz report while forwarding only the outward component can literally become an axis lock.

With full relative virtual-HID pointer forwarding, ordinary motion and Dock pressure use the same mechanism and no special edge-pressure state machine is needed in the canonical path.

## Virtual-HID wheel events are carriers, not final magnitudes

A conventional virtual mouse wheel is accepted as hardware-origin input, but its realized CoreGraphics magnitude is not a trustworthy linear representation of the report value.

The validated trace showed one observed scroll event per virtual-HID wheel report, yet repeated unit reports were expanded into strongly rate-dependent point deltas, reaching roughly 95 points. This persisted even when numeric scroll-acceleration properties read back as disabled and after an acceleration-support property experiment.

Do not compensate for this in the core transfer profile. That would mix OS transport behavior into the TrackPoint model.

The supported design is:

1. queue the exact core output;
2. send a tiny nonzero VHID wheel carrier;
3. observe the resulting hardware-origin event at an active CoreGraphics tap;
4. replace its delta, point, and fixed-point magnitudes with the queued target;
5. mark the event continuous.

The carrier establishes event provenance; the rewrite establishes magnitude.

If the rewrite tap cannot be created, fail the full-VHID scrolling path rather than silently falling back to conventional VHID wheel magnitude. The fallback is known to reintroduce the steep nonlinear response.

HID wheel sign conventions differ from the earlier Quartz-posted scroll convention. Test configured natural-scroll direction explicitly when changing transports.

## Karabiner virtual-HID lifecycle

Karabiner's root-only virtual-HID transport expects heartbeats. A client that only sends reports can appear to work once and then stop after idle when the service drops the connection.

Required behavior:

- send periodic heartbeats while idle;
- detect a dead connection;
- reconnect and reinitialize the virtual pointing device;
- retry the report that discovered the disconnect once;
- send an all-buttons-up report when the user daemon disconnects.

The root bridge's Unix socket should remain user-owned mode `0600`, and the helper should verify the peer UID. Keep the protocol limited to pointer/button/wheel state.

If Karabiner-Elements is configured to **modify the physical TrackPoint itself**, it can compete with this daemon for exclusive ownership. The project needs Karabiner's virtual-HID service, not Karabiner interception of the same physical pointing collection. If exclusive open fails despite confirmed Input Monitoring permission, check Karabiner Devices settings and other HID-grabbing software.

## Treat macOS privacy gates separately

There are three relevant runtime states:

- IOHID listen access / **Input Monitoring** for reading and seizing the physical device;
- CoreGraphics **PostEvent** access for the active scroll-rewrite path;
- Accessibility (**AX trust**) for the running process.

Do not collapse these into one "Accessibility" boolean. Request and log them separately.

The System Settings checkbox is not authoritative evidence that the current LaunchAgent process is trusted. The implementation calls runtime request APIs, including `CGRequestPostEventAccess()` and `AXIsProcessTrustedWithOptions(...prompt=true)`. A meaningful prompt can appear even when an old UI entry already looks enabled. After granting permission, restart the LaunchAgent and verify a fresh runtime log.

The strongest functional permission check is whether the required active event tap can actually be created.

### Code identity matters

TCC authorization is tied to code identity. A rebuilt ad-hoc-signed bundle can have a designated requirement based only on a build-specific `cdhash`; an existing privacy entry may therefore fail to authorize a new build.

For normal release/test packages that need authorization to remain meaningful across rebuilds, sign on the maintainer Mac with a stable Apple-issued identity and stable bundle identifier. Keep ad-hoc packages diagnostic-only.

Validate the installed app/LaunchAgent identity. A manual Terminal launch can have a different authorization context and is not a substitute for the automatic startup path.

## Debugging input-stack failures

### Measure boundaries before changing algorithms

When behavior feels nonlinear, laggy, or axis-locked, instrument adjacent layers:

```text
physical HID -> daemon raw values -> core output -> helper report
             -> observed macOS event -> rewritten/final event
```

Change one boundary at a time. The pointer deadzone was localized by preserving the same raw TrackPoint stream while switching only the final pointer transport.

For wheel behavior, count both carrier reports and observed events before assuming loss, duplication, or batching. A one-to-one count with changing magnitudes proves a transformation rather than transport loss.

After establishing the cause, remove temporary telemetry. Keep startup/health/error logs needed for permission, device ownership, event-tap, helper, and reconnect failures.

### Use explicit human-gated capture windows

For interactive remote tests, do not start a short capture window immediately after SSH or `sudo`; authentication and window switching can consume the measurement interval.

Prompt:

```text
Press Enter to begin:
```

and start the timed capture only afterward. For remote tests, using `/dev/tty` or uploading a temporary script is more robust than deeply nested shell quoting.

### Keep diagnostics portable

Assume macOS system shell tooling unless a test explicitly requires otherwise.

- prefer POSIX `sh`;
- avoid non-POSIX `awk` extensions;
- avoid shell-reserved names such as zsh's read-only `status`;
- avoid multiple layers of single-quoted shell/awk programs inside one SSH command;
- clear the relevant log and restart the service before drawing conclusions from startup output.

Stale accumulated logs can make a fixed or newly authorized build look broken.

### Separate correctness from tuning

First establish that pointer motion is responsive and two-dimensional, button transitions are balanced, Dock pressure is repeatable, and final scroll events have the intended magnitude and direction.

Only then tune `scroll_scale` or transfer-profile parameters. Changing the profile while the transport is still applying an unknown curve confounds the diagnosis.

## Release validation

Before merging or publishing a hardware-facing change, validate the packaged installation path, not only a source-tree build:

- gentle and fast pointer motion plus shallow diagonals;
- single/double clicks and click-drag;
- all relevant display edges, parallel motion, and immediate inward reversal;
- repeated Dock reveal and another reveal after more than 30 seconds idle;
- slow, medium, fast, horizontal, vertical, and diagonal middle-button scrolling;
- configured natural-scroll direction;
- one LaunchAgent restart followed by a short smoke test;
- a fresh startup log showing privacy states, rewrite-tap activation, helper connectivity, and seized-device match.

Configuration is read at daemon startup. After editing `~/.config/macOS-trackpoint-scroll.conf`, restart the LaunchAgent.
