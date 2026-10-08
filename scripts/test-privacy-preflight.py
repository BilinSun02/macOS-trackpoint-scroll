#!/usr/bin/env python3
"""Static regressions for the signed app's macOS TCC lifecycle.

Do not invoke --request-permissions on an unattended CI runner: it can
display a real system dialog. On-device behavior is an explicit acceptance
test in docs/PRIVACY_LIFECYCLE.md.
"""

from pathlib import Path
import plistlib
import re

root = Path(__file__).resolve().parents[1]
source = (root / "src/mac_trackpoint_scroll.c").read_text(encoding="utf-8")
installer = (root / "scripts/install-user.sh").read_text(encoding="utf-8")
spec = (root / "docs/PRIVACY_LIFECYCLE.md").read_text(encoding="utf-8")


def function_body(signature: str) -> str:
    assert source.count(signature) == 1, f"missing/ambiguous function: {signature}"
    # Functions of interest do not contain nested literal braces in strings.
    start = source.index(signature) + len(signature)
    opening = source.index("{", start)
    depth = 1
    for i in range(opening + 1, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : i]
    raise AssertionError(f"unclosed function: {signature}")


preflight = function_body("check_privacy_access(bool request)")
main = function_body("main(int argc, char **argv)")

for check in (
    "IOHIDCheckAccess(kIOHIDRequestTypeListenEvent)",
    "CGPreflightPostEventAccess()",
    "AXIsProcessTrusted()",
):
    assert check in preflight, f"missing TCC check: {check}"

def conditional_block(condition: str) -> str:
    marker = f"if ({condition}) {{"
    assert preflight.count(marker) == 1, f"missing/ambiguous guard: {marker}"
    opening = preflight.index(marker) + len(marker) - 1
    depth = 1
    for i in range(opening + 1, len(preflight)):
        if preflight[i] == "{":
            depth += 1
        elif preflight[i] == "}":
            depth -= 1
            if depth == 0:
                return preflight[opening + 1 : i]
    raise AssertionError(f"unclosed guard: {condition}")


# Every request appears exactly once, nested within the corresponding
# explicit request-mode conditional.
for condition, request in (
    ("request && access.listen != kIOHIDAccessTypeGranted",
     "IOHIDRequestAccess(kIOHIDRequestTypeListenEvent)"),
    ("request && !access.cg_post", "CGRequestPostEventAccess()"),
    ("request && !access.ax_trusted", "AXIsProcessTrustedWithOptions(options)"),
):
    assert preflight.count(request) == 1, f"unexpected request count: {request}"
    assert request in conditional_block(condition), f"unguarded TCC request: {request}"

assert "check_privacy_access(true)" in main
assert "check_privacy_access(false)" in main
assert 'argc == 2 && strcmp(argv[1], "--request-permissions") == 0' in main
assert main.index("check_privacy_access(true)") < main.index("setup_core(")
assert main.index("return 0;", main.index("check_privacy_access(true)")) < main.index(
    "setup_core("
), "explicit request mode must return before starting HID/event tap"

# A missing permission should make launchd treat initialization as a
# successful exit, preventing a permission-based restart loop.
assert "if (!privacy.cg_post || !privacy.ax_trusted)" in main
assert "if (privacy.listen != kIOHIDAccessTypeGranted)" in main
for marker in (
    "event tap blocked while TCC permission is",
    "HID open blocked while Input Monitoring is",
):
    assert marker in main, f"missing non-restarting diagnostic: {marker}"
assert len(re.findall(r"return 0;", main)) >= 4

assert "<key>RunAtLoad</key>" in installer
assert "<key>SuccessfulExit</key>" in installer
assert re.search(
    r"<key>KeepAlive</key>\s*<dict>\s*<key>SuccessfulExit</key>\s*<false/>\s*</dict>",
    installer,
)
assert "--request-permissions" in installer
assert "request-permissions" in spec

print("PASS: explicit TCC registration, noninteractive agent, and no permission restart storm")
