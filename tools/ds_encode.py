#!/usr/bin/env python3
"""Nintendo DS encoders, kept apart from the SNES ones on purpose.

`tools/pixel.py` is frozen -- it is what the oracle build encodes with, and the
whole value of the oracle is that its ROM does not move.  So the DS formats live
here instead, and nothing in this file is reachable from the SNES path.

THREE THINGS DIFFER, and each of them produces a plausible-looking wrong picture
rather than an error:

1.  4bpp TILE DATA.  The SNES is PLANAR -- bitplanes 0/1 interleaved by row for
    sixteen bytes, then 2/3 -- and the DS is LINEAR, two pixels to a byte with
    the LEFT pixel in the LOW nibble.  A SNES tile uploaded to a DS is not
    corrupt-looking noise; it is a recognisable but wrong pattern, which is worse.

2.  MAP ENTRY FLIP BITS.  Both machines pack a 10-bit tile index into a 16-bit
    entry, so the low ten bits agree.  Everything above them does not:

        SNES   v h p p p c c c c c c c c c c   bit15 V, bit14 H, 13 priority,
                                               12-10 palette
        DS     P P P P v h c c c c c c c c c c bit11 V, bit10 H, 15-12 palette

    Reuse the SNES packing and every flipped character comes out unflipped and
    on the wrong palette.

3.  PALETTE ENTRIES ARE THE SAME.  Both are little-endian 15-bit BGR,
    (b << 10) | (g << 5) | r.  This is the one place the two machines agree
    exactly, and it is worth stating because it looks like something that ought
    to differ -- the bytes are reused verbatim.
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from pixel import Canvas                                    # noqa: E402

# A DS text background's map entry.
DS_FLIP_H = 1 << 10
DS_FLIP_V = 1 << 11
DS_TILE_MASK = 0x03FF           # ten bits, so 1024 characters per background
DS_PAL_SHIFT = 12

# A single text background tops out at 64x64 characters.  Bigger than that is
# not a bigger background, it is a streamed window over a larger source map.
DS_BG_MAX_CHARS = 64


def tile_4bpp_ds(tile: list[list[int]]) -> bytes:
    """One 8x8 tile of palette indices as 32 bytes, DS-linear.

    Two pixels per byte, the LEFT pixel in the LOW nibble, rows top to bottom.
    Index 0 is transparent for sprites and for every background layer except the
    backmost, exactly as on the SNES.
    """
    out = bytearray()
    for y in range(8):
        for x in range(0, 8, 2):
            lo = tile[y][x] & 0x0F
            hi = tile[y][x + 1] & 0x0F
            out.append(lo | (hi << 4))
    return bytes(out)


def decode_tile_4bpp_ds(data: bytes, offset: int = 0) -> list[list[int]]:
    """The inverse, so a round trip can prove the encoder rather than assert it."""
    tile = []
    for y in range(8):
        row = []
        for x in range(4):
            b = data[offset + y * 4 + x]
            row.append(b & 0x0F)
            row.append((b >> 4) & 0x0F)
        tile.append(row)
    return tile


def ds_map_entry(index: int, flip_h: bool, flip_v: bool, palette: int = 0) -> int:
    if index > DS_TILE_MASK:
        raise SystemExit(f"character {index} does not fit a DS map entry's ten "
                         f"bits; a background holds {DS_TILE_MASK + 1}")
    if not 0 <= palette <= 15:
        raise SystemExit(f"palette {palette} is not one of the sixteen")
    return (index & DS_TILE_MASK) \
        | (DS_FLIP_H if flip_h else 0) \
        | (DS_FLIP_V if flip_v else 0) \
        | (palette << DS_PAL_SHIFT)


def bg_offset(x: int, y: int, width_chars: int, height_chars: int) -> int:
    """Where tile (x, y) lives in a DS text background's map.

    A text background is built from 32x32-character BLOCKS, not from rows.  The
    order is the one the SNES PPU uses too -- left block then right, top row of
    blocks then bottom -- so this formula is shared, and it is the single easiest
    thing in the DS's 2D setup to get subtly wrong.  Writing row-major instead
    shreds a 512-wide map into diagonal bands, which looks like a corrupt tileset
    rather than a layout bug.
    """
    if width_chars > 64 or height_chars > 64:
        raise SystemExit(f"{width_chars}x{height_chars} exceeds a single text "
                         f"background; stream a window instead")
    block = 0
    if x >= 32:
        block += 1
    if y >= 32:
        block += 2 if width_chars > 32 else 1
    return block * 1024 + (y % 32) * 32 + (x % 32)


def dedupe_tilemap_ds(world: Canvas, palette: int = 0):
    """Slice a painted world into characters, fold duplicates, emit DS bytes.

    Deliberately a separate function from build_assets.dedupe_tilemap rather than
    another branch inside it.  That one belongs to the frozen SNES path, and the
    two differ in both halves of what they do -- the character encoding and the
    entry packing -- so a shared body would be two `if target` tests wrapped
    around no shared code.

    The output map is ROW-MAJOR, and that is not the hardware layout.  Every DS
    scene here except the stations and the fragment is wider than the 64
    characters a single background holds, so the map cannot be uploaded as-is
    whatever order it is in: the renderer streams a window out of it and writes
    that window through bg_offset().  Row-major is the format a streamer wants to
    read, so it is what gets emitted, and bg_offset() is where the hardware
    layout is defined once for everybody.
    """
    cw, ch = world.w // 8, world.h // 8
    chars: list[bytes] = []
    index: dict[bytes, tuple[int, bool, bool]] = {}
    entries: list[int] = [0] * (cw * ch)

    for ty in range(ch):
        for tx in range(cw):
            tile = [[world.px[ty * 8 + y][tx * 8 + x] for x in range(8)]
                    for y in range(8)]
            flat = bytes(v for row in tile for v in row)

            hit = index.get(flat)
            if hit is None:
                h = bytes(v for row in tile for v in reversed(row))
                got = index.get(h)
                if got is not None:
                    hit = (got[0], True, False)
            if hit is None:
                v = bytes(v for row in reversed(tile) for v in row)
                got = index.get(v)
                if got is not None:
                    hit = (got[0], False, True)
            if hit is None:
                hv = bytes(v for row in reversed(tile) for v in reversed(row))
                got = index.get(hv)
                if got is not None:
                    hit = (got[0], True, True)
            if hit is None:
                num = len(chars)
                chars.append(tile_4bpp_ds(tile))
                index[flat] = (num, False, False)
                hit = (num, False, False)

            entries[ty * cw + tx] = ds_map_entry(hit[0], hit[1], hit[2], palette)

    tilemap = bytearray()
    for e in entries:
        tilemap += bytes((e & 0xFF, (e >> 8) & 0xFF))
    return b"".join(chars), bytes(tilemap), len(chars)


def encode_cels_ds(sheet: Canvas, cel: int, cols: int, rows: int) -> bytes:
    """A sprite sheet as cel-contiguous DS characters.

    Under the DS's 1D sprite mapping a 32x32 sprite is SIXTEEN CONSECUTIVE
    characters, so a page serialised row-major across its whole width is wrong --
    it interleaves four characters of one cel with four of the next, and every
    sprite comes out as a stripe of four different frames.

    The SNES sheet is already stored this way, because its streaming DMA uploads
    a frame as four 128-byte rows and so needed each cel contiguous too.  So the
    ORDER carries over unchanged and only the encoding of each character differs
    -- which is worth knowing, because it means a cel's index is the same number
    on both machines and nothing that refers to a frame has to be renumbered.
    """
    out = bytearray()
    for r in range(rows):
        for c in range(cols):
            sub = sheet.sub(c * cel, r * cel, cel, cel)
            for ty in range(cel // 8):
                for tx in range(cel // 8):
                    tile = [[sub.px[ty * 8 + y][tx * 8 + x] for x in range(8)]
                            for y in range(8)]
                    out += tile_4bpp_ds(tile)
    return bytes(out)


def encode_page_ds(canvas: Canvas) -> bytes:
    """A page of 8x8 characters, row-major.

    For a FONT, where every character stands alone and is addressed by its own
    index, row-major is right and cel ordering would be meaningless.  For
    anything made of multi-character sprites, use encode_cels_ds instead.
    """
    out = bytearray()
    for ty in range(canvas.h // 8):
        for tx in range(canvas.w // 8):
            tile = [[canvas.px[ty * 8 + y][tx * 8 + x] for x in range(8)]
                    for y in range(8)]
            out += tile_4bpp_ds(tile)
    return bytes(out)
