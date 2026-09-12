#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT"

# High-confidence credential/private-key formats only. Keep this conservative:
# the goal is to catch material that must never be published without turning
# ordinary source-code words like "token" or "secret" into false positives.
SECRET_RE="-----BEGIN ([A-Z0-9]+ )?PRIVATE KEY-----|gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}|A(K|S)IA[0-9A-Z]{16}|xox[baprs]-[A-Za-z0-9-]{10,}|AIza[0-9A-Za-z_-]{35}|sk_live_[0-9A-Za-z]{20,}|sk-[A-Za-z0-9_-]{32,}|(password|passwd|api[_-]?key|access[_-]?token|secret[_-]?key)[[:space:]]*[:=][[:space:]]*[\"']?[A-Za-z0-9/+_=.-]{12,}"

if [ "$(git rev-parse --is-shallow-repository 2>/dev/null || echo true)" = "true" ]; then
    echo "error: secret audit requires a full-history checkout (repository is shallow)" >&2
    exit 2
fi

TMP_LIST="$(mktemp -t macos-trackpoint-secrets-objects.XXXXXX)"
TMP_FINDINGS="$(mktemp -t macos-trackpoint-secrets-findings.XXXXXX)"
trap 'rm -f "$TMP_LIST" "$TMP_FINDINGS"' EXIT HUP INT TERM

: > "$TMP_FINDINGS"
git rev-list --objects --all > "$TMP_LIST"

while IFS=' ' read -r oid path; do
    [ -n "$oid" ] || continue
    [ "$(git cat-file -t "$oid" 2>/dev/null || true)" = "blob" ] || continue

    case "$path" in
        .env|.env.*|*/.env|*/.env.*|\
        id_rsa|id_dsa|id_ecdsa|id_ed25519|*/id_rsa|*/id_dsa|*/id_ecdsa|*/id_ed25519|\
        *.p12|*.pfx|*.key|*.pem|*.mobileprovision|\
        credentials|*/credentials|credentials.*|*/credentials.*)
            printf 'suspicious credential-bearing path: %s (%s)\n' \
                "${path:-unknown}" "$oid" >> "$TMP_FINDINGS"
            ;;
    esac

    if git cat-file blob "$oid" | LC_ALL=C grep -aEq -e "$SECRET_RE"; then
        printf 'possible secret pattern in blob: %s (%s)\n' \
            "${path:-unknown}" "$oid" >> "$TMP_FINDINGS"
    else
        status=$?
        if [ "$status" -ne 1 ]; then
            echo "error: grep failed while scanning blob $oid (${path:-unknown})" >&2
            exit "$status"
        fi
    fi
done < "$TMP_LIST"

# Commit/tag messages are reachable repository data too.
if git log --all --format='%H%n%B' | LC_ALL=C grep -aEq -e "$SECRET_RE"; then
    echo 'possible secret pattern in commit/tag metadata' >> "$TMP_FINDINGS"
else
    status=$?
    if [ "$status" -ne 1 ]; then
        echo 'error: grep failed while scanning commit/tag metadata' >&2
        exit "$status"
    fi
fi

# The core is a separate repository pinned as a submodule. Scan its checked-out
# source tree as part of package CI even though its independent history is not
# part of this repository's object database.
if [ -d core ] && git -C core rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    if git -C core ls-files | LC_ALL=C grep -Eq -e '(^|/)(\.env($|\.)|id_(rsa|dsa|ecdsa|ed25519)$|credentials($|\.)|.*\.(p12|pfx|key|pem|mobileprovision)$)'; then
        echo 'suspicious credential-bearing path in core submodule HEAD' >> "$TMP_FINDINGS"
    else
        status=$?
        if [ "$status" -ne 1 ]; then
            echo 'error: grep failed while scanning core submodule paths' >&2
            exit "$status"
        fi
    fi

    if git -C core grep -I -q -E -e "$SECRET_RE" HEAD -- .; then
        echo 'possible secret pattern in core submodule HEAD' >> "$TMP_FINDINGS"
    else
        status=$?
        if [ "$status" -ne 1 ]; then
            echo 'error: git grep failed while scanning core submodule HEAD' >&2
            exit "$status"
        fi
    fi
fi

if [ -s "$TMP_FINDINGS" ]; then
    echo 'error: repository secret audit found material requiring review:' >&2
    cat "$TMP_FINDINGS" >&2
    exit 1
fi

echo 'secret audit: PASS (full reachable history + core submodule HEAD)'
