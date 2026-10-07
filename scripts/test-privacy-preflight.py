#!/usr/bin/env python3
"""Regression guard: a KeepAlive LaunchAgent must not initiate TCC prompts."""

from pathlib import Path

source = (Path(__file__).resolve().parents[1] / "src/mac_trackpoint_scroll.c").read_text(
    encoding="utf-8"
)
marker = "static void\ncheck_privacy_access(void)\n{"
assert source.count(marker) == 1, "cannot locate privacy preflight"
body = source.split(marker, 1)[1].split("\n}\n", 1)[0]

for preflight in (
    "IOHIDCheckAccess(kIOHIDRequestTypeListenEvent)",
    "CGPreflightPostEventAccess()",
    "AXIsProcessTrusted()",
):
    assert preflight in body, f"missing non-interactive preflight: {preflight}"

for request in (
    "IOHIDRequestAccess(",
    "CGRequestPostEventAccess(",
    "AXIsProcessTrustedWithOptions(",
):
    assert request not in body, f"KeepAlive daemon must not initiate TCC prompt: {request}"

assert "input-monitoring=%s" in body and "cg-post-event=%s" in body
assert "ax-trusted=%s" in body
print("PASS: KeepAlive daemon checks privacy without requesting permission dialogs")
