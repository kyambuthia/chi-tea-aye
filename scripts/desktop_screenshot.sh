#!/usr/bin/env bash
# Runs the desktop build headless on the Xvfb display :98 and saves one frame.
# Usage: scripts/desktop_screenshot.sh [out.png] [seconds-before-capture]
# SDL3 prefers Wayland when WAYLAND_DISPLAY is set, which would open the window on the
# real desktop instead of Xvfb, so the X11 driver is forced here.
set -u
cd "$(dirname "$0")/.." || exit 1
out="${1:-/tmp/chi-desktop.png}"
wait_s="${2:-12}"
if ! pgrep -f "Xvfb :98" > /dev/null; then
    Xvfb :98 -screen 0 1280x720x24 > /dev/null 2>&1 &
    sleep 1
fi
DISPLAY=:98 SDL_VIDEO_DRIVER=x11 WAYLAND_DISPLAY= timeout $((wait_s + 5)) ./build/chi-tea-aye > /tmp/chi-desktop-run.log 2>&1 &
sleep "$wait_s"
if ffmpeg -loglevel error -y -f x11grab -video_size 1280x720 -i :98 -frames:v 1 "$out"; then
    echo "saved $out (game log: /tmp/chi-desktop-run.log)"
else
    echo "capture failed"; exit 1
fi
