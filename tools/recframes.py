#!/usr/bin/env python3
"""Pull individual frames out of a mednafen QuickTime recording.

Recording with `-qtrecord.vcodec raw` stores uncompressed top-down RGB frames
back to back inside the file's mdat atom, so a frame can be seeked to directly
without decoding anything.

  python3 tools/recframes.py rec.mov --frames 0 60 300 --out shots/
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

from PIL import Image


def find_mdat(path: Path) -> tuple[int, int]:
    """Return (offset, size) of the mdat payload."""
    with path.open("rb") as fh:
        pos = 0
        while True:
            fh.seek(pos)
            head = fh.read(8)
            if len(head) < 8:
                raise SystemExit(f"{path}: no mdat atom found")
            size, kind = struct.unpack(">I4s", head)
            body = pos + 8
            if size == 1:                       # 64-bit extended size
                size = struct.unpack(">Q", fh.read(8))[0]
                body = pos + 16
            elif size == 0:                     # runs to end of file
                size = path.stat().st_size - pos
            if kind == b"mdat":
                return body, pos + size - body
            pos += size


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("movie", type=Path)
    ap.add_argument("--width", type=int, default=512)
    ap.add_argument("--height", type=int, default=448)
    ap.add_argument("--frames", type=int, nargs="+", default=[0])
    ap.add_argument("--out", type=Path, default=Path("shots"))
    ap.add_argument("--prefix", default="frame")
    args = ap.parse_args()

    frame_bytes = args.width * args.height * 3
    offset, size = find_mdat(args.movie)
    total = size // frame_bytes
    print(f"{args.movie.name}: {total} frames of {args.width}x{args.height}")

    args.out.mkdir(parents=True, exist_ok=True)
    with args.movie.open("rb") as fh:
        for n in args.frames:
            if n >= total:
                print(f"  frame {n} past end ({total}), skipped")
                continue
            fh.seek(offset + n * frame_bytes)
            raw = fh.read(frame_bytes)
            img = Image.frombytes("RGB", (args.width, args.height), raw)
            dest = args.out / f"{args.prefix}_{n:05d}.png"
            img.save(dest)
            print(f"  {dest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
