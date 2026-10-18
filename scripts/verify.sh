#!/usr/bin/env bash
# Rebuilds the desktop and web targets, runs every check, and prints one summary.
# Usage: scripts/verify.sh [--no-web]
# Exits non-zero if a build or a check fails.
set -u
cd "$(dirname "$0")/.." || exit 1

fail=0
step() { printf '\n== %s\n' "$1"; }

step "desktop build (build/)"
if cmake --build build -j8 > /tmp/chi-desktop-build.log 2>&1; then
    echo "desktop build: ok"
else
    echo "desktop build: FAILED (see /tmp/chi-desktop-build.log)"; grep -m5 " error" /tmp/chi-desktop-build.log; fail=1
fi

if [ "${1:-}" != "--no-web" ]; then
    step "web build (build-web/)"
    if cmake --build build-web -j8 > /tmp/chi-web-build.log 2>&1; then
        echo "web build: ok (served from build-web/ on port 8000 if the server is running)"
    else
        echo "web build: FAILED (see /tmp/chi-web-build.log)"; grep -m5 " error" /tmp/chi-web-build.log; fail=1
    fi
fi

step "checks"
checks="movement_check weapon_state_check weapon_attachment_check hand_ik_check foot_contact_check clip_check clip_direction_check target_check wheel_check world_check vehicle_check render_math_check hud_check"
for t in $checks; do
    out=$(./build/$t 2>&1); rc=$?
    if [ $rc -eq 0 ]; then printf '  %-24s ok    %s\n' "$t" "$(echo "$out" | tail -1)"
    else printf '  %-24s FAIL\n%s\n' "$t" "$out"; fail=1; fi
done
# knee_check prints a table rather than a pass line; it must exit 0.
if ./build/knee_check > /dev/null 2>&1; then echo "  knee_check               ok"; else echo "  knee_check               FAIL"; fail=1; fi

step "result"
if [ $fail -eq 0 ]; then echo "all builds and checks passed"; else echo "FAILED: see above"; fi
exit $fail
