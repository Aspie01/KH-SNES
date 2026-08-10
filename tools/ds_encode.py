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

AND TWO THINGS THAT LOOK LIKE THEY SHOULD DIFFER AND DO NOT:

4.  SCREEN BLOCK PLACEMENT is identical.  Both machines build a background from
    32x32-character blocks placed SC0/SC1/SC2/SC3 at +0/+0x800/+0x1000/+0x1800,
    row-major inside each, and even number the size codes the same way
    (0 = 32x32, 1 = 64x32, 2 = 32x64, 3 = 64x64).  bg_entry_index() is shared.

5.  INDEX 0 means transparent on every layer of both machines, and what shows
    through is the backdrop -- entry 0 of SUB-PALETTE 0 specifically, at DS
    palette RAM 0x05000000.  Not entry 0 of whichever sub-palette the character
    named.  (This file used to claim index 0 was opaque on the backmost layer.
    It is not, on either machine; see docs/DS_FORMATS.md.)

Every claim in this file was checked against GBATEK v3.06 and fullsnes, and
cross-checked against libnds, by three independent readings.  What they found is
recorded in docs/DS_FORMATS.md -- including the four defects the check turned up,
of which the index-0 docstring above was one.
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))


def _pillow_or_explain() -> None:
    """Turn a bare ModuleNotFoundError into the thing to actually do.

    tools/pixel.py imports PIL, this file imports pixel, and build_assets.py
    imports this file -- so a machine without Pillow gets a three-frame
    traceback ending in `No module named 'PIL'` and no hint that the answer
    depends on WHICH python3 ran.  On Windows it usually does: devkitPro's MSYS2
    is where make must run, and it is the one python that cannot have Pillow.

    pixel.py is frozen (see docs/DS_PORT_PROMPT.md) and build_assets.py takes
    additions only, so the check lives here, on the import path both of them go
    through, and it is a function rather than a bare try at module scope.
    """
    try:
        import PIL                                     # noqa: F401
        return
    except ModuleNotFoundError:
        pass

    import platform
    where = platform.system()
    lines = [
        "",
        "Pillow is not installed for this python.",
        "",
        "  tools/pixel.py draws every asset as an indexed PNG and imports PIL,",
        "  so nothing is emitted without it:",
        "",
        "      pip install Pillow            # or: sudo apt install python3-pil",
        "",
    ]
    if where.startswith(("MSYS", "MINGW", "CYGWIN")):
        lines += [
            f"  BUT THIS PYTHON IS {where}'s -- {sys.executable} -- which on this",
            "  project means devkitPro's MSYS2, and there is no Pillow to install",
            "  there: its pacman python has no pip and the trimmed devkitPro",
            "  package set has no python-pillow.  Do not go looking for one.",
            "",
            "  Nothing in the pipeline needs that shell.  Only `make` does.  Run",
            "  these two with the ordinary Windows Python -- Git Bash, PowerShell",
            "  or cmd, pointed at this same folder:",
            "",
            "      python tools/build_assets.py",
            "      python tools/check_link.py",
            "",
            "  and then `make -C platform/ds` back here.  Both scripts resolve",
            "  their own paths from __file__, so the two shells build one tree.",
            "  See platform/ds/README.md, 'The Python lines are not bound by",
            "  any of this'.",
            "",
        ]
    raise SystemExit("\n".join(lines))


_pillow_or_explain()

from pixel import Canvas                                    # noqa: E402

# A DS text background's map entry.
DS_FLIP_H = 1 << 10
DS_FLIP_V = 1 << 11
DS_TILE_MASK = 0x03FF           # ten bits, so 1024 characters per background
DS_PAL_SHIFT = 12

# A single text background tops out at 64x64 characters.  Bigger than that is
# not a bigger background, it is a streamed window over a larger source map.
DS_BG_MAX_CHARS = 64

# BGxCNT's size field, and the only four shapes a text background comes in.  A
# map has to be stored WITH its size code: 64x32 and 32x64 both put their second
# block at +0x800 and differ only in whether that block is the right half or the
# bottom half, so a map emitted without the code renders correctly in one and
# transposed in the other.
DS_BG_SIZES = {(32, 32): 0, (64, 32): 1, (32, 64): 2, (64, 64): 3}

# Sprites.  DISPCNT bit 4 selects 1D mapping and bits 20-21 the boundary; at the
# default boundary of 32 a character's tile number is its byte offset / 32, so
# numbers step by one and a 4bpp 32x32 cel is sixteen consecutive numbers.
#
# THE BYTES NEVER CHANGE WITH THE BOUNDARY AND THE NUMBERS ALWAYS DO.  That is
# the whole hazard: raise the boundary to 64 and every cel's tile number halves,
# so the numbering has to be generated against the boundary the runtime sets and
# cannot be a constant in a table.  The reason anyone would raise it is right
# here -- the tile number is ten bits, so at boundary 32 it reaches only the
# first 32 KiB of object VRAM however much of it the machine has.
DS_OBJ_BOUNDARY = 32
DS_OBJ_REACH = (DS_TILE_MASK + 1) * DS_OBJ_BOUNDARY      # 32768 bytes


def ds_bg_size(width_chars: int, height_chars: int) -> int:
    """BGxCNT's two-bit size code for a background of this shape."""
    code = DS_BG_SIZES.get((width_chars, height_chars))
    if code is None:
        raise SystemExit(f"{width_chars}x{height_chars} is not one of the four "
                         f"text-background shapes {sorted(DS_BG_SIZES)}")
    return code


def ds_bg_fit(width_chars: int, height_chars: int):
    """The smallest text background a map this size fits in, or None.

    Returns (width, height, size code).  None means it does not fit in one
    background at all and has to be streamed -- which is most of this game's
    scenes, the island being 128x64 characters.
    """
    for (w, h), code in sorted(DS_BG_SIZES.items(), key=lambda kv: kv[0][0] * kv[0][1]):
        if width_chars <= w and height_chars <= h:
            return w, h, code
    return None


def obj_tile_number(byte_offset: int, boundary: int = DS_OBJ_BOUNDARY) -> int:
    """The OAM tile number that addresses this byte of object VRAM.

    Refuses rather than truncates.  An offset that is not a multiple of the
    boundary is not addressable at all, and one past the ten-bit reach silently
    aliases back to the start of the page, which on a DS looks like the wrong
    sprite rather than like a fault.
    """
    if byte_offset % boundary:
        raise SystemExit(f"object VRAM offset {byte_offset} is not a multiple of "
                         f"the {boundary}-byte 1D boundary, so no tile number "
                         f"addresses it")
    n = byte_offset // boundary
    if n > DS_TILE_MASK:
        raise SystemExit(f"object VRAM offset {byte_offset} needs tile number "
                         f"{n}, past the ten-bit field; at boundary {boundary} "
                         f"the reach is {DS_TILE_MASK * boundary} bytes")
    return n


def tile_4bpp_ds(tile: list[list[int]]) -> bytes:
    """One 8x8 tile of palette indices as 32 bytes, DS-linear.

    Two pixels per byte, the LEFT pixel in the LOW nibble, rows top to bottom.
    GBATEK: "the lower 4 bits define the color for the left (!) dot" -- the
    exclamation mark is theirs, and earned.

    Index 0 is transparent on every layer and in every sub-palette, exactly as on
    the SNES.  What shows through it is the backdrop, which is entry 0 of
    sub-palette 0 and of no other, so a scene moved off sub-palette 0 does not
    take its own colour 0 with it.
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


def bg_entry_index(x: int, y: int, width_chars: int, height_chars: int) -> int:
    """Which ENTRY of a DS text background's map holds character (x, y).

    Entries, not bytes -- the unit is in the name because getting it wrong is a
    factor of two that still lands inside the map and so still draws something.
    Multiply by two for a byte offset.

    A text background is built from 32x32-character BLOCKS, not from rows.  The
    order is the one the SNES PPU uses too -- left block then right, top row of
    blocks then bottom -- so this formula is shared, and it is the single easiest
    thing in the DS's 2D setup to get subtly wrong.  Emitting a flat 64-wide
    row-major array for a 512-wide layer does not look corrupt: the hardware
    reads entry y*32+x for the left block, so the picture comes out horizontally
    halved, vertically doubled, and split left/right by source row band.

    The two 2-block shapes are the trap.  64x32 and 32x64 both put their second
    block at entry 1024, and only width_chars says whether that block is the
    right half or the bottom half -- which is why the shape is a parameter and
    not derived from x and y.

    A coordinate outside the background WRAPS, because that is what the hardware
    does ("When the screen is scrolled it'll always wraparound") and because a
    streamer scrolling a window off the edge of a background depends on it.  The
    wrap has to be applied here rather than left to the caller: a raw y of 64 in a
    64x64 background would otherwise compute block 4 and write 2 KB past the map.
    """
    if width_chars > 64 or height_chars > 64:
        raise SystemExit(f"{width_chars}x{height_chars} exceeds a single text "
                         f"background; stream a window instead")
    x %= width_chars
    y %= height_chars
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
    that window through bg_entry_index().  Row-major is the format a streamer wants
    to read, so it is what gets emitted, and bg_entry_index() is where the hardware
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
