#!/usr/bin/env python3
"""Shared pixel-art helpers, palettes, and SNES encoders.

Everything the art scripts draw is an *indexed* image: a 2D array of palette
indices, never RGB.  That is the same constraint the hardware imposes, so a
mistake shows up here rather than as garbled colour on the console.
"""
from __future__ import annotations

from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "assets" / "src"
GEN = ROOT / "assets" / "gen"


# ---------------------------------------------------------------------------
# Palettes.  Each is at most 16 RGB triples; index 0 is transparent for
# sprites and the backdrop for backgrounds.
# ---------------------------------------------------------------------------

BG_GROUND = [
    (24, 56, 120),      # 0  deep ocean, also the backdrop colour
    (247, 226, 173),    # 1  sand light
    (222, 191, 129),    # 2  sand mid
    (186, 150, 94),     # 3  sand dark
    (150, 200, 96),     # 4  grass light
    (96, 160, 72),      # 5  grass mid
    (56, 112, 56),      # 6  grass dark
    (120, 200, 224),    # 7  water light
    (56, 136, 200),     # 8  water mid
    (32, 88, 160),      # 9  water deep
    (176, 128, 80),     # 10 wood light
    (112, 76, 48),      # 11 wood dark
    (176, 176, 168),    # 12 rock light
    (104, 104, 112),    # 13 rock dark
    (255, 255, 255),    # 14 foam
    (48, 48, 72),       # 15 outline
]

OBJ_SORA = [
    (0, 0, 0),          # 0  transparent
    (32, 24, 40),       # 1  outline
    (255, 216, 176),    # 2  skin light
    (232, 176, 128),    # 3  skin mid
    (184, 128, 96),     # 4  skin shadow
    (216, 140, 64),     # 5  hair light
    (160, 92, 40),      # 6  hair mid
    (232, 72, 72),      # 7  jumpsuit red
    (160, 40, 48),      # 8  jumpsuit red dark
    (80, 88, 128),      # 9  navy light
    (40, 44, 72),       # 10 navy dark
    (248, 248, 248),    # 11 white (gloves, collar)
    (248, 200, 72),     # 12 shoe yellow
    (200, 144, 40),     # 13 shoe yellow dark
    (208, 216, 232),    # 14 keyblade shaft
    (240, 200, 96),     # 15 keyblade gold
]

# The same ground after dark, entry for entry against BG_GROUND: the island at
# night is one palette upload, not a second tileset.
#
# Written out rather than derived, because a single darkening curve does not
# work.  Desaturating everything toward blue -- which is what night vision
# really does -- collapses sand and rock into the same grey, and the west cliff
# stops reading as a cliff.  So the sand is kept warm and the rock kept cool,
# far enough apart to tell at a glance, and the surf stays bright because it is
# the one thing a storm actually lights up.
BG_NIGHT = [
    (8, 10, 26),        # 0  the sea at night -- and the void it falls into
    (104, 92, 96),      # 1  sand light, warm so it does not read as stone
    (80, 70, 78),       # 2  sand mid
    (58, 50, 62),       # 3  sand dark
    (56, 84, 68),       # 4  grass light
    (40, 62, 54),       # 5  grass mid
    (26, 44, 42),       # 6  grass dark
    (56, 96, 146),      # 7  water light
    (26, 58, 110),      # 8  water mid
    (14, 32, 74),       # 9  water deep
    (78, 58, 52),       # 10 wood light
    (48, 34, 34),       # 11 wood dark
    (76, 84, 110),      # 12 rock light, cool against the warm sand
    (46, 52, 76),       # 13 rock dark
    (152, 170, 210),    # 14 moonlight on the surf
    (16, 16, 30),       # 15 outline
]

OBJ_HEART = [
    (0, 0, 0),          # 0  transparent
    (6, 6, 12),         # 1  outline
    (40, 34, 62),       # 2  body
    (86, 78, 130),      # 3  body highlight
    (255, 232, 80),     # 4  eye
    (240, 168, 40),     # 5  eye rim
    (72, 64, 104),      # 6  antenna / sinew
    (20, 20, 32),       # 7  underside
    (148, 138, 200),    # 8  rim light along the boss's edges
    (128, 52, 132),     # 9  the heart-shaped hole
    (208, 104, 208),    # 10 hole glow
    (60, 52, 94),       # 11 torso mid
    (22, 20, 36),       # 12 torso deep
    (96, 86, 146),      # 13 highlight
    (255, 255, 255),    # 14 spark
    (0, 0, 0),          # 15
]

OBJ_SCENE = [
    (0, 0, 0),          # 0  transparent
    (32, 40, 32),       # 1  outline
    (152, 216, 104),    # 2  leaf light
    (88, 160, 64),      # 3  leaf mid
    (48, 104, 48),      # 4  leaf dark
    (184, 144, 96),     # 5  trunk light
    (136, 96, 56),      # 6  trunk mid
    (88, 60, 40),       # 7  trunk dark
    (200, 200, 192),    # 8  rock light
    (152, 152, 152),    # 9  rock mid
    (96, 96, 104),      # 10 rock dark
    (112, 72, 40),      # 11 coconut
    (64, 128, 56),      # 12 leaf spine
    (0, 0, 0), (0, 0, 0), (0, 0, 0),
]

# The palms and the rocks, after dark.  Without this the one tree still
# standing on the fragment is the brightest thing on the screen.
def _dim(rgb: tuple[int, int, int], keep: float = 0.42) -> tuple[int, int, int]:
    r, g, b = rgb
    return (int(r * keep), int(g * keep), int(b * keep + 18))


OBJ_SCENE_NIGHT = [OBJ_SCENE[0]] + [_dim(c) for c in OBJ_SCENE[1:]]

OBJ_FX = [
    (0, 0, 0),          # 0  transparent
    (255, 255, 255),    # 1  core
    (216, 244, 255),    # 2  inner glow
    (136, 208, 248),    # 3  mid
    (72, 136, 224),     # 4  outer
    (0, 0, 0), (0, 0, 0), (0, 0, 0),
    (0, 0, 0), (0, 0, 0), (0, 0, 0), (0, 0, 0),
    (0, 0, 0), (0, 0, 0), (0, 0, 0), (0, 0, 0),
]

# Drawn with half-add colour math against BG1, so "black" reads as a 50%
# darkening of whatever ground is underneath rather than a black blob.
OBJ_SHADOW = [(0, 0, 0)] * 16

# 2bpp, so only four entries.  Index 3 is the opaque box background baked
# into every glyph cell, which is what lets text sit inside a dialogue window
# on a single-layer BG.
HUD_PAL = [
    (0, 0, 0),          # 0  transparent
    (248, 248, 248),    # 1  ink
    (144, 176, 232),    # 2  trim / window edge
    (24, 28, 56),       # 3  box background
]

# Station of Awakening: the stained-glass platform the game opens on.
BG_DIVE = [
    (0, 0, 0),          # 0  the void around the platform
    (20, 20, 56),       # 1  glass, deepest
    (44, 52, 112),      # 2  glass, mid blue
    (96, 112, 192),     # 3  glass, light blue
    (240, 208, 96),     # 4  gold leading
    (176, 136, 48),     # 5  gold, shadowed
    (216, 64, 72),      # 6  red
    (144, 32, 52),      # 7  red, deep
    (248, 228, 128),    # 8  pale yellow
    (255, 216, 176),    # 9  skin
    (36, 28, 48),       # 10 hair / dark leading
    (248, 248, 248),    # 11 white
    (88, 160, 96),      # 12 green
    (140, 88, 176),     # 13 violet
    (72, 156, 168),     # 14 teal
    (12, 12, 28),       # 15 outline
]

# Pedestals and the three dream weapons.
OBJ_DIVE = [
    (0, 0, 0),          # 0  transparent
    (16, 16, 36),       # 1  outline
    (152, 160, 200),    # 2  stone light
    (104, 112, 160),    # 3  stone mid
    (64, 72, 112),      # 4  stone dark
    (232, 236, 248),    # 5  blade / metal light
    (168, 176, 208),    # 6  metal mid
    (112, 120, 152),    # 7  metal dark
    (240, 208, 96),     # 8  gold
    (176, 136, 48),     # 9  gold dark
    (216, 64, 72),      # 10 red
    (144, 32, 52),      # 11 red dark
    (96, 144, 224),     # 12 blue
    (56, 88, 168),      # 13 blue dark
    (248, 236, 160),    # 14 glow
    (120, 88, 56),      # 15 wood
]

# The five islanders share one palette, so five heads of hair have to fit
# alongside a single skin ramp and the handful of clothing colours between
# them.  Entries 4-8 are the hair; everything from 9 down is shared.
OBJ_ISLE = [
    (0, 0, 0),          # 0  transparent
    (32, 24, 40),       # 1  outline
    (255, 216, 176),    # 2  skin light
    (224, 168, 124),    # 3  skin mid
    (198, 78, 84),      # 4  Kairi, auburn
    (222, 226, 236),    # 5  Riku, silver
    (248, 216, 96),     # 6  Tidus, blond
    (150, 96, 48),      # 7  Selphie, brown
    (236, 124, 44),     # 8  Wakka, orange
    (248, 248, 248),    # 9  white
    (72, 112, 200),     # 10 blue
    (248, 200, 72),     # 11 yellow
    (156, 92, 172),     # 12 purple
    (44, 48, 80),       # 13 navy
    (96, 176, 88),      # 14 green
    (192, 60, 64),      # 15 red
]

# OBJ palette 1 while the island is falling.  Riku and Kairi are the only
# islanders left out there, so the three colours the others took -- Tidus's
# blond, Selphie's brown, Wakka's orange -- are given over to the Heartless,
# and the Shadows are drawn against those indices instead of their own page.
OBJ_NIGHT = list(OBJ_ISLE)
OBJ_NIGHT[6] = (34, 28, 54)         # Heartless body
OBJ_NIGHT[7] = (74, 66, 112)        # ...and its highlight
OBJ_NIGHT[8] = (255, 232, 80)       # eyes




# ---------------------------------------------------------------------------
# Canvas
# ---------------------------------------------------------------------------

class Canvas:
    """A rectangle of palette indices."""

    def __init__(self, w: int, h: int, fill: int = 0):
        self.w = w
        self.h = h
        self.px = [[fill] * w for _ in range(h)]

    def set(self, x: int, y: int, c: int) -> None:
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y][x] = c

    def get(self, x: int, y: int) -> int:
        if 0 <= x < self.w and 0 <= y < self.h:
            return self.px[y][x]
        return 0

    def rect(self, x0: int, y0: int, x1: int, y1: int, c: int) -> None:
        """Filled rectangle, inclusive of both corners."""
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.set(x, y, c)

    def hline(self, x0: int, x1: int, y: int, c: int) -> None:
        for x in range(min(x0, x1), max(x0, x1) + 1):
            self.set(x, y, c)

    def vline(self, x: int, y0: int, y1: int, c: int) -> None:
        for y in range(min(y0, y1), max(y0, y1) + 1):
            self.set(x, y, c)

    def ellipse(self, cx: float, cy: float, rx: float, ry: float, c: int) -> None:
        """Filled ellipse; rx/ry are half-extents in pixels."""
        if rx <= 0 or ry <= 0:
            return
        y0, y1 = int(cy - ry - 1), int(cy + ry + 1)
        x0, x1 = int(cx - rx - 1), int(cx + rx + 1)
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                dx = (x + 0.5 - cx) / rx
                dy = (y + 0.5 - cy) / ry
                if dx * dx + dy * dy <= 1.0:
                    self.set(x, y, c)

    def blit(self, other: "Canvas", ox: int, oy: int, transparent: int = 0) -> None:
        for y in range(other.h):
            for x in range(other.w):
                v = other.px[y][x]
                if v != transparent:
                    self.set(ox + x, oy + y, v)

    def outline(self, color: int, over: int = 0) -> None:
        """Trace a one-pixel border around every non-empty run."""
        add = []
        for y in range(self.h):
            for x in range(self.w):
                if self.px[y][x] != over:
                    continue
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < self.w and 0 <= ny < self.h and \
                            self.px[ny][nx] not in (over, color):
                        add.append((x, y))
                        break
        for x, y in add:
            self.px[y][x] = color

    def rim_light(self, colour: int, skip: tuple[int, ...] = ()) -> None:
        """Light the upward-facing edge of every solid run.

        A near-black character on bright ground reads as a hole unless its
        edges catch something; this is cheaper than shading every plane.
        """
        add = []
        for y in range(self.h):
            for x in range(self.w):
                v = self.px[y][x]
                if v == 0 or v in skip:
                    continue
                if self.get(x, y - 1) == 0:
                    add.append((x, y))
        for x, y in add:
            self.px[y][x] = colour

    def sub(self, x0: int, y0: int, w: int, h: int) -> "Canvas":
        out = Canvas(w, h)
        for y in range(h):
            for x in range(w):
                out.px[y][x] = self.get(x0 + x, y0 + y)
        return out

    def flip_h(self) -> "Canvas":
        out = Canvas(self.w, self.h)
        for y in range(self.h):
            out.px[y] = list(reversed(self.px[y]))
        return out

    def max_index(self) -> int:
        return max((max(row) for row in self.px), default=0)


# ---------------------------------------------------------------------------
# PNG output (previews and editable sources)
# ---------------------------------------------------------------------------

def write_png(canvas: Canvas, palette: list[tuple[int, int, int]],
              path: Path, transparent0: bool = True) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    img = Image.new("P", (canvas.w, canvas.h))
    flat: list[int] = []
    for rgb in palette:
        flat.extend(rgb)
    flat.extend([0, 0, 0] * (256 - len(palette)))
    img.putpalette(flat)
    img.putdata([canvas.px[y][x] for y in range(canvas.h) for x in range(canvas.w)])
    if transparent0:
        img.info["transparency"] = 0
    img.save(path)


def read_png_indexed(path: Path) -> Canvas:
    img = Image.open(path)
    if img.mode != "P":
        raise SystemExit(f"{path}: expected an indexed (palette) PNG, got {img.mode}")
    c = Canvas(img.width, img.height)
    data = list(img.getdata())
    for y in range(img.height):
        row = data[y * img.width:(y + 1) * img.width]
        c.px[y] = list(row)
    return c


# ---------------------------------------------------------------------------
# SNES encoders
# ---------------------------------------------------------------------------

def snes_color(rgb: tuple[int, int, int]) -> int:
    """Pack an 8-bit RGB triple into the SNES 15-bit BGR word."""
    r, g, b = (v >> 3 for v in rgb)
    return (b << 10) | (g << 5) | r


def palette_bytes(palette: list[tuple[int, int, int]], count: int = 16) -> bytes:
    out = bytearray()
    for i in range(count):
        rgb = palette[i] if i < len(palette) else (0, 0, 0)
        v = snes_color(rgb)
        out += bytes((v & 0xFF, v >> 8))
    return bytes(out)


def tile_4bpp(tile: list[list[int]]) -> bytes:
    """Encode one 8x8 tile of palette indices as 32 bytes.

    SNES 4bpp stores bitplanes 0/1 interleaved by row for the first 16 bytes,
    then bitplanes 2/3 the same way.
    """
    out = bytearray()
    for plane_pair in (0, 2):
        for y in range(8):
            lo = hi = 0
            for x in range(8):
                v = tile[y][x] & 0x0F
                bit = 7 - x
                if v & (1 << plane_pair):
                    lo |= 1 << bit
                if v & (1 << (plane_pair + 1)):
                    hi |= 1 << bit
            out.append(lo)
            out.append(hi)
    return bytes(out)


def tile_2bpp(tile: list[list[int]]) -> bytes:
    """Encode one 8x8 tile as 16 bytes of 2bpp."""
    out = bytearray()
    for y in range(8):
        lo = hi = 0
        for x in range(8):
            v = tile[y][x] & 0x03
            bit = 7 - x
            if v & 1:
                lo |= 1 << bit
            if v & 2:
                hi |= 1 << bit
        out.append(lo)
        out.append(hi)
    return bytes(out)


def cut_tiles(canvas: Canvas) -> list[list[list[int]]]:
    """Slice a canvas into row-major 8x8 tiles."""
    tiles = []
    for ty in range(canvas.h // 8):
        for tx in range(canvas.w // 8):
            tiles.append([[canvas.px[ty * 8 + y][tx * 8 + x] for x in range(8)]
                          for y in range(8)])
    return tiles


def encode_4bpp_page(canvas: Canvas) -> bytes:
    return b"".join(tile_4bpp(t) for t in cut_tiles(canvas))


def encode_2bpp_page(canvas: Canvas) -> bytes:
    return b"".join(tile_2bpp(t) for t in cut_tiles(canvas))


def write_bin(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
