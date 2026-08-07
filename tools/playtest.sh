#!/usr/bin/env bash
# Drive the ROM with scripted controller input and capture frames.
#
#   tools/playtest.sh kh.sfc out/ "none:60" "up+right:90" "b:20" ...
#
# Each step is "keys:frames": the keys are held for that many 60 Hz frames and
# a screenshot is taken at the end of the step.  Use "none" to just wait.
# Keys: up down left right a b x y start select
#
# Mednafen is launched from a shell rather than from Python on purpose --
# under this sandbox a Python-spawned mednafen never maps its X window.
set -euo pipefail

ROM="${1:?usage: playtest.sh <rom> <outdir> [step ...]}"
OUT="${2:?usage: playtest.sh <rom> <outdir> [step ...]}"
shift 2

DISPLAY_NUM=":99"
MDF_HOME="$OUT/.mdf"
ROM_STEM="$(basename "${ROM%.*}")"

mkdir -p "$OUT" "$MDF_HOME"
export DISPLAY="$DISPLAY_NUM"

cleanup() {
    [[ -n "${EMU_PID:-}" ]] && kill "$EMU_PID" 2>/dev/null || true
    [[ -n "${XVFB_PID:-}" ]] && kill "$XVFB_PID" 2>/dev/null || true
}
trap cleanup EXIT

pkill -f "Xvfb $DISPLAY_NUM" 2>/dev/null || true
pkill mednafen 2>/dev/null || true
sleep 1

Xvfb "$DISPLAY_NUM" -screen 0 1280x1024x24 -nolisten tcp >/dev/null 2>&1 &
XVFB_PID=$!
sleep 2

HOME="$MDF_HOME" SDL_AUDIODRIVER=dummy /usr/games/mednafen \
    -sound 0 -video.driver softfb -snes.xscale 2 -snes.yscale 2 \
    "$ROM" > "$OUT/mednafen.log" 2>&1 &
EMU_PID=$!

# Wait for the window; mednafen titles it after the ROM's basename.
WIN=""
for _ in $(seq 1 40); do
    WIN="$(xdotool search --name "^${ROM_STEM}$" 2>/dev/null | tail -1 || true)"
    [[ -n "$WIN" ]] && break
    sleep 0.5
done
if [[ -z "$WIN" ]]; then
    echo "error: mednafen never opened a window" >&2
    tail -5 "$OUT/mednafen.log" >&2
    exit 1
fi

shoot() {
    local name="$1"
    xwd -id "$WIN" -silent > "$OUT/$name.xwd"
    python3 - "$OUT/$name.xwd" "$OUT/$name.png" <<'PY'
import sys
sys.path.insert(0, "tools")
from pathlib import Path
from playtest import read_xwd
read_xwd(Path(sys.argv[1])).save(sys.argv[2])
PY
    rm -f "$OUT/$name.xwd"
    echo "$OUT/$name.png"
}

sleep 3
shoot "00_boot"

n=0
for step in "$@"; do
    n=$((n + 1))
    spec="${step%%:*}"
    frames="${step##*:}"
    keys=()
    if [[ "$spec" != "none" ]]; then
        IFS='+' read -ra parts <<< "$spec"
        for k in "${parts[@]}"; do
            case "$k" in
                up) keys+=(w) ;;   down) keys+=(s) ;;
                left) keys+=(a) ;; right) keys+=(d) ;;
                a) keys+=(KP_6) ;; b) keys+=(KP_2) ;;
                x) keys+=(KP_4) ;; y) keys+=(KP_8) ;;
                start) keys+=(Return) ;; select) keys+=(Tab) ;;
                *) echo "unknown key '$k'" >&2; exit 1 ;;
            esac
        done
    fi

    for k in "${keys[@]:-}"; do
        [[ -n "$k" ]] && xdotool keydown --window "$WIN" "$k"
    done
    python3 -c "import time,sys; time.sleep(int(sys.argv[1])/60.0)" "$frames"
    for k in "${keys[@]:-}"; do
        [[ -n "$k" ]] && xdotool keyup --window "$WIN" "$k"
    done

    shoot "$(printf '%02d_%s' "$n" "$spec")"
done
