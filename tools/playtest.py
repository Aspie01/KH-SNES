#!/usr/bin/env python3
"""Run the ROM under a virtual X display, drive it with real controller input,
and dump frames as PNGs.

This is the only way to check the parts that cannot be verified statically:
whether the ground lands where it should, whether depth sorting puts
Sora behind the right palm, and whether the swing connects.

  python3 tools/playtest.py kh.sfc --out shots/

Each --script step is "keys:frames", where keys are held for that many frames
and a screenshot is taken at the end of the step.
"""
from __future__ import annotations

import argparse
import os
import shutil
import signal
import struct
import subprocess
import sys
import time
from pathlib import Path

from PIL import Image

DISPLAY = ":99"
SCREEN = "1280x1024x24"

# Mednafen's stock SNES bindings, as X keysyms.
KEYS = {
    "up": "w", "down": "s", "left": "a", "right": "d",
    "a": "KP_6", "b": "KP_2", "x": "KP_4", "y": "KP_8",
    "start": "Return", "select": "Tab",
}


def read_xwd(path: Path) -> Image.Image:
    """Decode an X window dump.  Pillow has no XWD reader, but the format is
    a fixed big-endian header followed by raw pixels."""
    raw = path.read_bytes()
    hdr = struct.unpack(">25I", raw[:100])
    (header_size, _version, pix_format, depth, width, height, _xoff,
     _byte_order, _bmp_unit, _bmp_bit_order, _bmp_pad, bpp, bytes_per_line,
     _vclass, red_mask, green_mask, blue_mask, _bits_rgb, _cmap_entries,
     ncolors) = hdr[:20]

    if pix_format != 2:
        raise SystemExit(f"{path}: expected ZPixmap, got format {pix_format}")
    if bpp not in (24, 32):
        raise SystemExit(f"{path}: unsupported depth {bpp}")

    offset = header_size + ncolors * 12
    pixels = raw[offset:]

    def shift_of(mask: int) -> int:
        s = 0
        while mask and not (mask & 1):
            mask >>= 1
            s += 1
        return s

    if (bpp == 32 and red_mask == 0x00FF0000
            and green_mask == 0x0000FF00 and blue_mask == 0x000000FF):
        return Image.frombuffer("RGBA", (width, height), pixels,
                                "raw", "BGRA", bytes_per_line, 1).convert("RGB")

    rs, gs, bs = shift_of(red_mask), shift_of(green_mask), shift_of(blue_mask)
    step = bpp // 8
    out = Image.new("RGB", (width, height))
    data = []
    for y in range(height):
        row = pixels[y * bytes_per_line:(y + 1) * bytes_per_line]
        for x in range(width):
            chunk = row[x * step:x * step + step]
            if len(chunk) < step:
                data.append((0, 0, 0))
                continue
            v = int.from_bytes(chunk[:4].ljust(4, b"\0"), "little")
            data.append((((v & red_mask) >> rs) & 0xFF,
                         ((v & green_mask) >> gs) & 0xFF,
                         ((v & blue_mask) >> bs) & 0xFF))
    out.putdata(data)
    return out


def crop_to_content(img: Image.Image) -> Image.Image:
    """Trim the black desktop around mednafen's window."""
    bbox = img.convert("L").point(lambda v: 255 if v > 8 else 0).getbbox()
    return img.crop(bbox) if bbox else img


class Session:
    def __init__(self, rom: Path, outdir: Path, scale: int):
        self.rom = rom
        self.outdir = outdir
        self.scale = scale
        self.xvfb: subprocess.Popen | None = None
        self.emu: subprocess.Popen | None = None
        self.home = outdir / ".mdf"
        self._win: str | None = None

    def __enter__(self) -> "Session":
        self.outdir.mkdir(parents=True, exist_ok=True)
        self.home.mkdir(parents=True, exist_ok=True)
        self.xvfb = subprocess.Popen(
            ["Xvfb", DISPLAY, "-screen", "0", SCREEN, "-nolisten", "tcp"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(1.5)

        env = dict(os.environ, DISPLAY=DISPLAY, HOME=str(self.home),
                   SDL_AUDIODRIVER="dummy")
        self.emu = subprocess.Popen(
            ["/usr/games/mednafen", "-sound", "0", "-video.fs", "0",
             "-video.driver", "softfb",
             "-snes.xscale", "2", "-snes.yscale", "2", str(self.rom)],
            env=env, stdout=open(self.outdir / "mednafen.log", "w"),
            stderr=subprocess.STDOUT)
        time.sleep(5.0)                 # let the emulator open its window
        if self.emu.poll() is not None:
            raise SystemExit("mednafen exited immediately")
        return self

    def __exit__(self, *_exc) -> None:
        for proc in (self.emu, self.xvfb):
            if proc and proc.poll() is None:
                proc.send_signal(signal.SIGTERM)
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()

    def _xdo(self, *args: str) -> None:
        subprocess.run(["xdotool", *args],
                       env=dict(os.environ, DISPLAY=DISPLAY),
                       check=False, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)

    def hold(self, keys: list[str], frames: int) -> None:
        """Hold buttons for roughly `frames` frames at 60 Hz."""
        syms = [KEYS[k] for k in keys if k in KEYS]
        for s in syms:
            self._xdo("keydown", "--window", self._window(), s)
        time.sleep(max(frames, 1) / 60.0)
        for s in syms:
            self._xdo("keyup", "--window", self._window(), s)

    def _window(self) -> str:
        """Mednafen titles its window after the ROM; wait for it to appear."""
        if self._win:
            return self._win
        deadline = time.time() + 25.0
        while time.time() < deadline:
            res = subprocess.run(
                ["xdotool", "search", "--name", f"^{self.rom.stem}$"],
                env=dict(os.environ, DISPLAY=DISPLAY),
                capture_output=True, text=True)
            ids = [ln for ln in res.stdout.split() if ln.strip()]
            if ids:
                self._win = ids[-1]
                return self._win
            time.sleep(0.5)
        raise SystemExit("mednafen never opened a window")

    def shot(self, name: str) -> Path:
        dump = self.outdir / f"{name}.xwd"
        with open(dump, "wb") as fh:
            subprocess.run(["xwd", "-id", self._window(), "-silent"],
                           env=dict(os.environ, DISPLAY=DISPLAY),
                           stdout=fh, check=True)
        img = read_xwd(dump)
        if self.scale > 1:
            img = img.resize((img.width * self.scale, img.height * self.scale),
                             Image.NEAREST)
        png = self.outdir / f"{name}.png"
        img.save(png)
        dump.unlink()
        return png


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path)
    ap.add_argument("--out", type=Path, default=Path("shots"))
    ap.add_argument("--scale", type=int, default=2)
    ap.add_argument("--script", nargs="*", default=["none:60"],
                    help="steps of the form keys:frames, e.g. up+right:40 b:20")
    args = ap.parse_args()

    if not shutil.which("Xvfb"):
        raise SystemExit("Xvfb is required")

    with Session(args.rom, args.out, args.scale) as s:
        shots = [s.shot("00_boot")]
        for n, step in enumerate(args.script, start=1):
            spec, _, frames = step.partition(":")
            keys = [] if spec in ("-", "", "none") else spec.split("+")
            s.hold(keys, int(frames or 30))
            shots.append(s.shot(f"{n:02d}_{spec}"))
        for p in shots:
            print(p)
    return 0


if __name__ == "__main__":
    sys.exit(main())
