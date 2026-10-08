# TrackPoint macOS privacy lifecycle specification

Status: implementation contract. This document distinguishes behavior enforced by the
repository from macOS/TCC behavior that requires validation on a real Mac.

## Background and regression history

The signed user-side application needs protected HID input to seize the physical
TrackPoint, and CoreGraphics authorization to run an active scroll-rewrite event
tap. These are separate from the root virtual-HID bridge and Karabiner.

In September 2026, manually enabling the installed `.app` under Accessibility
and Input Monitoring was not always sufficient to authorize the **running**
LaunchAgent. The working remedy included invoking the relevant runtime
authorization APIs from the actual signed TrackPoint process. Specifically,
an Accessibility prompt appeared even though the Settings entry was already
enabled; after approving it, the active rewrite tap worked. PostEvent and
AX-trust preflights are distinct and must be reported separately.

A later October change removed all request APIs from the daemon to prevent
repeated prompts on automatic restarts. That correctly stopped automatic
prompting but also removed the already-demonstrated registration/recovery path.
Do not substitute manual Settings toggles or TCC resets for that path.

## Safety invariants

1. Ordinary LaunchAgent startups must **never initiate a TCC prompt**.
   They may perform read-only authorization checks and use the working
   HID/event-tap paths. Report Input Monitoring, CoreGraphics PostEvent and AX
   trust independently. Successful event-tap creation and HID opening are the
   functional acceptance criteria; a Settings checkbox is not sufficient.
2. The signed, installed `macOS-trackpoint-scroll` executable must provide
   a dedicated `--request-permissions` CLI mode. It invokes the existing
   runtime authorization mechanisms from that same executable, at most once
   per gate per explicit invocation:
   - `IOHIDRequestAccess(kIOHIDRequestTypeListenEvent)` when HID listen
     permission is not already granted. Even a `denied` state may need a
     registration request, not just an `unknown` state.
   - `CGRequestPostEventAccess()` when the PostEvent preflight denies.
   - `AXIsProcessTrustedWithOptions` with
     `kAXTrustedCheckOptionPrompt=true` when AX trust is absent.
   This mode must neither create a CGEventTap nor seize hardware nor connect
   to the root bridge. It prints diagnostics and exits; it must not wait for
   asynchronous dialog resolution or assert that permission has been granted.
3. Privacy-blocked *initialization* must not trigger an automatic prompt or
   an unbounded launchd restart loop. Use a distinct, successful exit for
   startup failures where the corresponding permission preflight is missing.
   Preserve restart-on-unexpected-failure for failures that are not explained
   by missing permissions. Avoid prematurely rejecting a working tap merely
   because an advisory preflight reports a denial.
4. Retain `RunAtLoad` and use launchd `KeepAlive` conditioned on
   `SuccessfulExit=false`: successful exit after a blocked startup stays
   stopped, while an unexpected nonzero termination is restarted. Once the
   user grants permissions, they explicitly restart the LaunchAgent.
5. Do not silently re-sign, rotate the certificate, modify TCC databases,
   reset unrelated applications' permissions, or delete users' config files.
   Maintain the stable installed bundle path, bundle identifier and signing
   identity already used by NewOSSetup.

## User recovery procedure

After installing a build implementing this specification:

```sh
LABEL=io.github.bilinsun02.macos-trackpoint-scroll
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
BIN="$HOME/Applications/macOS-trackpoint-scroll.app/Contents/MacOS/macOS-trackpoint-scroll"

launchctl bootout "gui/$(id -u)" "$PLIST" 2>/dev/null || true
"$BIN" --request-permissions
```

Approve any macOS request, or manually enable the **installed**
`~/Applications/macOS-trackpoint-scroll.app` in Privacy & Security →
Input Monitoring and Accessibility. Then:

```sh
launchctl bootstrap "gui/$(id -u)" "$PLIST"
tail -n 30 "$HOME/Library/Logs/macOS-trackpoint-scroll/stderr.log"
```

Healthy diagnostics ordinarily report
`input-monitoring=granted cg-post-event=granted ax-trusted=yes`,
followed by active scroll-rewrite tap creation and a running HID daemon.
A prompt may register the app without immediately changing the preflight
return value; the subsequent fresh startup, not the request mode, validates
success. If trust remains denied, investigate signing identity and actual TCC
attribution instead of repeating resets.

## Testable acceptance criteria

- Source-level regression checks ensure request APIs are absent from the
  background preflight and present only in explicit request mode.
- CLI smoke tests establish `--request-permissions` is a recognized option
  without calling it on an unattended CI runner (which could open dialogs).
- launchd plist tests require `SuccessfulExit=false`, not unconditional
  `KeepAlive=true`.
- A successful-exit path for a missing corresponding permission is covered
  by checks/tests, without converting unrelated initialization failures into
  success.
- On real macOS hardware: absent grants cause no popup storm and no restart
  storm; a single explicit request can register/promote authorization; after
  approval/restart the HID seize and active rewrite tap work; TCC identity
  remains stable across a rebuild. CI alone cannot establish these behaviors.

Reference: Apple AXIsProcessTrustedWithOptions documentation states that
prompting is asynchronous and does not change the immediate result.
