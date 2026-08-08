#!/usr/bin/env python3
"""Generate every art and data asset the ROM links against.

Outputs into assets/gen (binaries the assembler includes) and assets/src
(indexed PNGs, so the art can be inspected or hand-redrawn and re-imported).

All artwork here is original.  Nothing is traced, ripped, or imported from a
commercial release; the palettes and proportions are written from scratch to
sit inside SNES limits (16 colours per palette, 4bpp tiles, 256-tile pages).
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from ds_encode import (                                # noqa: E402
    DS_BG_MAX_CHARS, DS_OBJ_BOUNDARY, DS_OBJ_REACH, decode_tile_4bpp_ds,
    dedupe_tilemap_ds, ds_bg_fit, encode_cels_ds, encode_page_ds,
)
from pixel import (                                    # noqa: E402
    BG_DIVE, BG_GROUND, BG_NIGHT, BG_TOWN, HUD_PAL, OBJ_ARMOR, OBJ_DIVE,
    OBJ_FX, OBJ_HEART, OBJ_ISLE, OBJ_NIGHT, OBJ_SCENE, OBJ_SCENE_NIGHT,
    OBJ_SHADOW, OBJ_SORA, OBJ_TOWN,
    Canvas, GEN, ROOT, SRC, cut_tiles, encode_2bpp_page, encode_4bpp_page,
    palette_bytes, tile_4bpp, write_bin, write_png,
)

TILE = 16                               # ground tile, square
# The ORIGINAL map size, and now only a default.  A map carries its own
# dimensions (see Grid), because the DS worlds are larger than the SNES ones and
# a global here was the single thing preventing that.
#
# The SNES is stuck at these numbers and always will be: BG1's tilemap is 64x32
# characters and BG3's map sits immediately after it in VRAM, so there is nowhere
# for a wider one to go.  Its five maps stay this size, which is what keeps the
# frozen build byte-identical and usable as an oracle.
MAP_W, MAP_H = 32, 16
TILEMAP_W, TILEMAP_H = 64, 32           # PPU tilemap, in 8x8 characters
WORLD_W, WORLD_H = TILEMAP_W * 8, TILEMAP_H * 8
assert (MAP_W * TILE, MAP_H * TILE) == (WORLD_W, WORLD_H)

# A DS 2D background holds at most 64x64 characters, which is 32x32 of our 16 px
# tiles.  Past that the 2D ground renderer has to stream a window as the camera
# crosses tile boundaries; the 3D quad ground has no such limit.  Exceeding it is
# a deliberate choice, not an accident -- see docs/WORLD_SIZES.md.
DS_BG_TILES = 32


class Grid:
    """A map and its dimensions, read from the file rather than assumed."""

    __slots__ = ("rows", "w", "h", "name")

    def __init__(self, rows: list[str], name: str = ""):
        self.rows = rows
        self.h = len(rows)
        self.w = len(rows[0]) if rows else 0
        self.name = name

    def __getitem__(self, j):
        return self.rows[j]

    def at(self, i, j):
        """The code at a cell, or None off the map -- what the rim test wants."""
        if 0 <= i < self.w and 0 <= j < self.h:
            return self.rows[j][i]
        return None

    @property
    def px(self):
        return self.w * TILE, self.h * TILE

STEP = 8                                # pixels one height step lifts a tile

# Ground palette indices, named for legibility below.
SAND_L, SAND_M, SAND_D = 1, 2, 3
GRASS_L, GRASS_M, GRASS_D = 4, 5, 6
WATER_L, WATER_M, WATER_D = 7, 8, 9
WOOD_L, WOOD_D = 10, 11
ROCK_L, ROCK_D = 12, 13
FOAM, OUTLINE = 14, 15

# The same sixteen slots, named again for Traverse Town.  A scene's palette is
# what gives them meaning, so the town reuses the island's indices rather than
# needing any more of them: sand becomes cobble, grass becomes warm paving,
# rock becomes plaster, wood becomes beams.
COBBLE_L, COBBLE_M, COBBLE_D = 1, 2, 3
PAVE_L, PAVE_M, PAVE_D = 4, 5, 6
PLASTER_L, PLASTER_M, PLASTER_D = 7, 8, 9
BEAM_L, BEAM_D = 10, 11
LAMPLIGHT, DEEP = 12, 13
GLINT = 14

# terrain code -> (light, mid, dark, walkable, height)
#
# Height is in eight-pixel steps.  The engine allows a move only between tiles
# one step or less apart, so a deck at +2 is sealed off except over its step.
TERRAIN = {
    "~": (WATER_M, WATER_D, WATER_D, False, 0),
    "-": (WATER_L, WATER_M, WATER_D, False, 0),
    ".": (SAND_L, SAND_M, SAND_D, True, 0),
    ",": (GRASS_L, GRASS_M, GRASS_D, True, 0),
    "=": (WOOD_L, WOOD_L, WOOD_D, True, 0),
    # A cliff top is the lit surface; its face below is the dark one, which is
    # the whole of what makes a ledge read as a ledge from overhead.
    "#": (ROCK_L, ROCK_L, OUTLINE, False, 3),
    "T": (GRASS_L, GRASS_M, GRASS_D, False, 0),
    "R": (GRASS_L, GRASS_M, GRASS_D, False, 0),
    "r": (SAND_L, SAND_M, SAND_D, False, 0),
    "B": (WOOD_L, WOOD_L, WOOD_D, True, 1),
    "L": (WOOD_L, WOOD_L, WOOD_D, True, 1),
    "P": (WOOD_L, WOOD_L, WOOD_D, True, 2),
    "H": (WOOD_L, WOOD_L, WOOD_D, True, 2),
    # Day two's corner of the island
    "W": (FOAM, WATER_L, WATER_M, True, 0),     # the pool under the fall
    "C": (SAND_M, SAND_D, OUTLINE, True, 0),    # the Secret Place
    "b": (GRASS_L, GRASS_M, GRASS_D, False, 0),  # a bush
    "Y": (GRASS_L, GRASS_M, GRASS_D, False, 0),  # a palm carrying coconuts
    "M": (WOOD_L, WOOD_L, WOOD_D, True, 2),      # the leaning trunk
    "K": (GRASS_L, GRASS_M, GRASS_D, True, 3),   # its leafy top
    "F": (ROCK_L, ROCK_L, OUTLINE, False, 3),    # cliff with the waterfall
    # Nothing at all: the dark the island falls into.  Index 0 is the
    # backdrop colour, so a void tile costs one character and no palette.
    "*": (0, 0, 0, False, 0),
    #--- Traverse Town ------------------------------------------------------
    "c": (COBBLE_L, COBBLE_M, COBBLE_D, True, 0),    # cobbles
    "p": (PAVE_L, PAVE_M, PAVE_D, True, 0),          # paving, under a lamp
    # Warm stone, not cobbles: a raised walkway in the same colours as the
    # square around it reads as flat ground with an inexplicable seam, because
    # the only difference is the 16 px face at its southern edge.
    "s": (PAVE_L, PAVE_M, PAVE_D, True, 1),          # a step up
    "q": (PAVE_L, PAVE_M, PAVE_D, True, 2),          # a raised walkway
    "w": (PLASTER_L, PLASTER_L, PLASTER_D, False, 3),   # a building
    "e": (PLASTER_L, PLASTER_L, PLASTER_D, False, 3),   # ...with a lit window
    "o": (BEAM_L, BEAM_L, BEAM_D, False, 3),         # a roof over the street
    "d": (LAMPLIGHT, PAVE_D, BEAM_D, True, 0),       # a doorway to somewhere
    "x": (BEAM_L, BEAM_D, OUTLINE, False, 0),        # crates
    "l": (COBBLE_L, COBBLE_M, COBBLE_D, False, 0),   # a lamp post
    "n": (PLASTER_L, PLASTER_M, PLASTER_D, False, 0),   # the fountain rim
    "v": (GLINT, DEEP, OUTLINE, False, 0),           # ...and what is in it
    #--- Stations of Awakening ----------------------------------------------
    # A station is painted by build_dive_platform() rather than by build_world(),
    # so this code is never drawn and its colours are never read.  It exists so
    # that a station can present the same (walkable, height) interface every
    # authored map does -- see grid_from_coll() -- and so the checker's messages
    # name something meaningful instead of a stand-in.  Off the platform is "*",
    # the void, which already means exactly the right thing.
    "G": (0, 0, 0, True, 0),                         # standable stained glass
}


# ---------------------------------------------------------------------------
# Ground
#
# Seen from three-quarters overhead, the way A Link to the Past and Secret of
# Mana are.  A tile is a plain 16x16 square, so the map is a lookup rather
# than a projection, and the whole terrain vocabulary folds down to a few
# dozen 8x8 characters.
#
# Height still matters: a tile at h steps is painted 8*h pixels higher than
# its cell with a cliff face filling the gap back down, which is what turns a
# height difference into a visible ledge.
# ---------------------------------------------------------------------------

# Tiles only need a rim where they meet a *different* surface.  Drawing the
# bevel unconditionally turns the ground into a visible lattice, which is the
# usual way square tiling gives itself away.
GROUP = {"~": "water", "-": "water", ".": "sand", "r": "sand",
         ",": "grass", "T": "grass", "R": "grass", "=": "wood", "#": "rock",
         "B": "wood", "L": "wood", "P": "wood", "H": "wood",
         "W": "water", "C": "cave", "b": "grass", "Y": "grass",
         "M": "wood", "K": "grass", "F": "rock", "*": "void",
         "c": "cobble", "s": "pave", "q": "pave", "p": "pave",
         "w": "plaster", "e": "plaster", "o": "beam", "d": "door",
         "x": "beam", "l": "cobble", "n": "plaster", "v": "water",
         "G": "void"}    # never drawn; see TERRAIN

# What the side of a raised block is made of.  The leafy top of the climbing
# tree is grass, but what holds it up is a trunk; a bridge has nothing under
# it but the water it crosses.
FACE = {"B": "water", "L": "wood", "P": "wood", "H": "wood", "M": "wood",
        "K": "wood", "#": "rock", "F": "rock",
        "w": "stone", "e": "stone", "o": "beam", "s": "stone", "q": "stone"}

# Tiles whose whole shape is the drawing, so a border would only fight it,
# and the void, which has no shape at all.
NO_RIM = "K*l"

# Speckle is per-tile rather than per-cell so that every sand tile is the same
# four characters.  Two phases, alternating on (i + j), is enough to break up
# the repeat without doubling the budget twice over.
SAND_SPECKLE = ((((3, 4), SAND_L), ((11, 6), SAND_L), ((7, 11), SAND_L),
                 ((13, 12), SAND_D), ((5, 8), SAND_D)),
                (((6, 3), SAND_L), ((13, 8), SAND_L), ((2, 12), SAND_L),
                 ((9, 5), SAND_D), ((11, 13), SAND_D)))
GRASS_SPECKLE = ((((4, 3), GRASS_D), ((12, 5), GRASS_D), ((7, 10), GRASS_D),
                  ((2, 8), GRASS_L), ((14, 12), GRASS_L)),
                 (((9, 2), GRASS_D), ((3, 7), GRASS_D), ((13, 11), GRASS_D),
                  ((6, 13), GRASS_L), ((11, 6), GRASS_L)))


def water_texture(c: Canvas, phase: int, light: int) -> None:
    """Drifting highlights, so open water is not a flat field."""
    if phase == 0:
        c.hline(2, 7, 4, light)
        c.hline(9, 13, 9, light)
        c.hline(4, 6, 12, light)
    else:
        c.hline(8, 13, 3, light)
        c.hline(1, 5, 8, light)
        c.hline(10, 14, 13, light)


def plank_texture(c: Canvas) -> None:
    """Boards running east-west, with a seam every four pixels."""
    for y in range(0, 16, 4):
        c.hline(0, 15, y, WOOD_D)
    c.vline(7, 0, 15, WOOD_D)


def draw_tile(code: str, phase: int, edges: dict[str, str | None]) -> Canvas:
    """One 16x16 ground tile, aware of what it borders on each side."""
    light, mid, dark, _, _ = TERRAIN[code]
    me = GROUP[code]
    c = Canvas(TILE, TILE, mid)

    if code in "~-":
        water_texture(c, phase, light)
    elif code == "W":
        # The plunge pool: churned white, not the flat sea.
        water_texture(c, phase, FOAM)
        for (px, py) in ((3, 3), (12, 6), (6, 12), (13, 12)):
            c.ellipse(px, py, 2.2, 1.4, FOAM)
    elif code in ".r":
        for (tx, ty), col in SAND_SPECKLE[phase]:
            c.set(tx, ty, col)
    elif code in ",TRY":
        for (tx, ty), col in GRASS_SPECKLE[phase]:
            c.set(tx, ty, col)
    elif code == "b":
        # A bush: three overlapping clumps, so it reads as foliage and not
        # just a darker patch of lawn.
        for (bx, by, r) in ((5, 9, 4.2), (11, 7, 4.0), (8, 4, 3.6)):
            c.ellipse(bx, by, r, r * 0.8, GRASS_D)
            c.ellipse(bx, by - 1, r * 0.6, r * 0.5, GRASS_M)
        c.set(3, 6, GRASS_L)
        c.set(13, 4, GRASS_L)
    elif code == "K":
        # A canopy seen from above.  Round, with the corners left dark, or a
        # treetop is indistinguishable from a square of lawn.
        c.rect(0, 0, 15, 15, OUTLINE)
        c.ellipse(7.5, 7.5, 8.6, 8.6, GRASS_D)
        for (fx, fy) in ((4, 5), (11, 4), (12, 10), (6, 11), (8, 7)):
            c.ellipse(fx, fy, 3.2, 2.6, GRASS_M)
            c.ellipse(fx, fy - 1, 2.0, 1.4, GRASS_L)
    elif code in "=LPHM":
        plank_texture(c)
    elif code == "B":
        # A bridge is planks laid over the water it crosses, so the tile has
        # to paint both: open sea to either side of the walkway, and a rope
        # rail along each edge.
        for y in range(16):
            for x in range(16):
                c.set(x, y, WATER_D)
        water_texture(c, phase, WATER_M)
        c.rect(0, 2, 15, 13, WOOD_L)
        for y in range(3, 13, 3):
            c.hline(0, 15, y, WOOD_D)
        c.hline(0, 15, 2, WOOD_D)
        c.hline(0, 15, 13, WOOD_D)
        c.hline(0, 15, 1, ROCK_D)               # the rails
        c.hline(0, 15, 14, ROCK_D)
    elif code in "#F":
        for y in range(2, 15, 4):
            for x in range((y // 2) % 3, 16, 5):
                c.set(x, y, ROCK_D)
        c.set(3, 11, ROCK_D)
        c.set(12, 5, ROCK_D)
    elif code in "cl":
        # Cobbles: staggered courses, small enough that the eye reads texture
        # rather than a grid.
        for y in range(1, 16, 3):
            row = (y // 3) & 1
            for x in range((0 if row else 2), 16, 4):
                c.hline(x, x + 2, y, COBBLE_L if (x + y) % 5 else COBBLE_D)
                c.set(x + 3, y, COBBLE_D)
            c.hline(0, 15, y + 2, COBBLE_D)
        if code == "l":
            # Only the foot of the post and the light around it: the post
            # itself is a sprite, so that it stands up and sorts with everyone
            # walking past it.
            c.ellipse(8, 11, 7.0, 4.0, PAVE_M)
            c.ellipse(8, 11, 4.4, 2.4, PAVE_L)
            c.ellipse(8, 12, 2.4, 1.2, OUTLINE)
    elif code in "psq":
        # Flagstones, larger and warmer -- the paving a lamp lights, and the
        # raised walkways, which are built of the same stone.
        c.rect(0, 0, 15, 15, PAVE_M)
        for y in (0, 8):
            for x in (0, 8):
                c.rect(x + 1, y + 1, x + 6, y + 6, PAVE_L)
                c.hline(x + 1, x + 6, y + 6, PAVE_D)
                c.vline(x + 6, y + 1, y + 6, PAVE_D)
    elif code in "we":
        # The top of a building is roof, whichever it is: a lit window only
        # makes sense on the face, and only the bottom row of a block shows a
        # face at all.  That is what `e` is for.
        for y in range(3, 16, 5):
            c.hline(0, 15, y, PLASTER_M)
        for x in range(5, 16, 6):
            c.set(x, 1, PLASTER_M)
            c.set(x, 11, PLASTER_M)
    elif code == "o":
        # Pantiles, seen almost from above.
        for y in range(0, 16, 4):
            c.hline(0, 15, y, BEAM_D)
            c.hline(0, 15, y + 1, BEAM_L)
        for x in range(0, 16, 5):
            c.vline(x, 0, 15, BEAM_D)
    elif code == "d":
        # A doorway: dark inside, with the light of the next district coming
        # up the step.
        c.rect(0, 0, 15, 15, PAVE_D)
        c.rect(2, 0, 13, 13, OUTLINE)
        c.rect(3, 1, 12, 12, BEAM_D)
        c.ellipse(8, 1, 5.0, 3.0, BEAM_D)
        c.rect(5, 3, 10, 12, LAMPLIGHT)
        c.ellipse(8, 3, 3.0, 2.0, LAMPLIGHT)
        c.hline(2, 13, 13, BEAM_L)
        c.hline(1, 14, 14, PAVE_L)
        c.hline(0, 15, 15, PAVE_M)
    elif code == "x":
        # Crates stacked against a wall.
        c.rect(0, 0, 15, 15, COBBLE_M)
        for (bx, by, bw, bh) in ((0, 4, 9, 11), (9, 7, 7, 8)):
            c.rect(bx, by, bx + bw - 1, by + bh - 1, BEAM_L)
            c.rect(bx + 1, by + 1, bx + bw - 2, by + bh - 2, BEAM_D)
            c.rect(bx + 2, by + 2, bx + bw - 3, by + bh - 3, BEAM_L)
            c.hline(bx, bx + bw - 1, by, OUTLINE)
            c.vline(bx, by, by + bh - 1, OUTLINE)
    elif code == "n":
        # The rim of the fountain in the middle of the square.
        c.rect(0, 0, 15, 15, PLASTER_M)
        c.hline(0, 15, 0, PLASTER_L)
        c.hline(0, 15, 15, PLASTER_D)
        for x in range(1, 16, 4):
            c.vline(x, 2, 13, PLASTER_L)
    elif code == "v":
        # ...and the water, which nobody has turned off.
        c.rect(0, 0, 15, 15, DEEP)
        for (wy, x0, x1) in ((3, 2, 7), (7, 9, 14), (11, 4, 9)):
            c.hline(x0, x1, wy, GLINT)
        c.set(6, 6, GLINT)
        c.set(11, 12, GLINT)
    elif code == "C":
        # Dirt trodden flat over years, so the chamber floor reads as
        # something other than more of the rock around it.
        for (tx, ty) in ((3, 4), (11, 3), (6, 9), (14, 11), (8, 13)):
            c.set(tx, ty, OUTLINE)
        c.set(5, 6, SAND_M)
        c.set(12, 8, SAND_M)

    if code in NO_RIM:
        return c

    def edge_colour(neighbour: str | None, upper: bool) -> int | None:
        if neighbour is None:
            return None
        other = GROUP[neighbour]
        if other == me:
            return None
        if other == "water" and me != "water":
            return FOAM                 # a shoreline, not just a seam
        return light if upper else dark

    # Lit from the north-west, so the two near edges catch the light and the
    # two far ones fall away.
    n = edge_colour(edges["n"], True)
    w = edge_colour(edges["w"], True)
    s = edge_colour(edges["s"], False)
    e = edge_colour(edges["e"], False)
    if n is not None:
        c.hline(0, 15, 0, n)
    if w is not None:
        c.vline(0, 0, 15, w)
    if s is not None:
        c.hline(0, 15, 15, s)
    if e is not None:
        c.vline(15, 0, 15, e)
    return c


def draw_block(code: str, phase: int, edges: dict[str, str | None],
               height: int) -> Canvas:
    """A ground tile plus, for a raised one, the cliff face under it.

    The canvas reaches back down to where the tile would sit at ground level,
    so the caller blits it at `wy - STEP * height` and the face lands exactly
    on the cell the tile belongs to.
    """
    top = draw_tile(code, phase, edges)
    lift = STEP * height
    if lift == 0:
        return top

    c = Canvas(TILE, TILE + lift)
    kind = FACE.get(code, "rock")
    if kind == "rock":
        c.rect(0, TILE, TILE - 1, TILE + lift - 1, ROCK_D)
        for (cx, cy) in ((3, 2), (3, 3), (3, 4), (10, 1), (10, 2), (10, 3),
                         (6, 5), (13, 4), (13, 5)):
            if cy < lift:
                c.set(cx, TILE + cy, OUTLINE)   # cracks down the face
    elif kind == "water":
        # A bridge stands on nothing: what shows under the planks is the
        # water, in shadow.
        c.rect(0, TILE, TILE - 1, TILE + lift - 1, WATER_D)
    elif kind == "stone":
        # The face of a building, which is where its windows are.
        c.rect(0, TILE, TILE - 1, TILE + lift - 1, PLASTER_M)
        for y in range(2, lift, 5):
            c.hline(0, TILE - 1, TILE + y, PLASTER_D)
        for x in range(3, TILE, 6):
            c.vline(x, TILE + 1, TILE + lift - 1, PLASTER_D)
        if code == "e" and lift >= 16:
            # A window with somebody still behind it -- the only thing that
            # tells you the town is inhabited before you meet anybody.
            c.rect(3, TILE + 4, 12, TILE + 16, OUTLINE)
            c.rect(4, TILE + 5, 11, TILE + 15, LAMPLIGHT)
            c.vline(7, TILE + 5, TILE + 15, OUTLINE)
            c.vline(8, TILE + 5, TILE + 15, OUTLINE)
            c.hline(4, 11, TILE + 9, OUTLINE)
            c.set(5, TILE + 6, GLINT)
            c.set(10, TILE + 6, GLINT)
    elif kind == "beam":
        # A roof overhangs the street it covers.
        c.rect(0, TILE, TILE - 1, TILE + lift - 1, BEAM_D)
        c.hline(0, TILE - 1, TILE + 1, BEAM_L)
    else:
        # A skirt of boards with the corner posts picked out, which is how the
        # island's walkways are built.
        c.rect(0, TILE, TILE - 1, TILE + lift - 1, WOOD_D)
        c.vline(3, TILE, TILE + lift - 1, WOOD_L)
        c.vline(11, TILE, TILE + lift - 1, WOOD_L)
    c.hline(0, TILE - 1, TILE, OUTLINE)         # the lip under the top face
    if kind != "water":
        c.hline(0, TILE - 1, TILE + lift - 1, OUTLINE)      # and its shadow

    if code == "F":
        # The fall itself: a column of white water down the cliff, edged in
        # blue and breaking into spray where it lands.
        for x in range(4, 12):
            for y in range(TILE - 2, TILE + lift):
                if x in (4, 11):
                    col = WATER_M
                elif (x * 3 + y * 5) % 7 == 0:
                    col = WATER_L
                else:
                    col = FOAM
                c.set(x, y, col)
        c.ellipse(8, TILE + lift - 2, 6.0, 2.0, FOAM)
        c.ellipse(8, TILE + lift - 1, 7.0, 1.4, WATER_L)

    c.blit(top, 0, 0, transparent=-1)
    if code == "F":
        # ...and over the lip, so the water reads as coming off the top.
        for x in range(5, 11):
            for y in range(TILE - 5, TILE):
                c.set(x, y, FOAM if (x + y) % 3 else WATER_L)
    return c


def build_world(grid) -> tuple[Canvas, bytes, bytes]:
    """Paint a map, north to south, and derive its collision and height maps.

    Accepts a Grid, or a bare list of rows for the original 32x16 callers.
    """
    if not isinstance(grid, Grid):
        grid = Grid(list(grid))
    px_w, px_h = grid.px
    world = Canvas(px_w, px_h, WATER_D)

    # North to south, so a raised block paints over the bottom of whatever is
    # behind it and is painted over in turn by whatever is in front.  That
    # single ordering is the whole of the depth logic for the ground.
    for j in range(grid.h):
        for i in range(grid.w):
            code = grid[j][i]
            height = TERRAIN[code][4]
            edges = {"n": grid.at(i, j - 1), "s": grid.at(i, j + 1),
                     "w": grid.at(i - 1, j), "e": grid.at(i + 1, j)}
            tile = draw_block(code, (i + j) & 1, edges, height)
            world.blit(tile, i * TILE, j * TILE - STEP * height,
                       transparent=-1)

    coll = bytearray(grid.w * grid.h)
    hmap = bytearray(grid.w * grid.h)
    for j in range(grid.h):
        for i in range(grid.w):
            coll[j * grid.w + i] = 1 if TERRAIN[grid[j][i]][3] else 0
            hmap[j * grid.w + i] = TERRAIN[grid[j][i]][4]
    return world, bytes(coll), bytes(hmap)


def dedupe_tilemap(world: Canvas, layout: str = "snes") -> tuple[bytes, bytes, int]:
    """Slice the painted world into 8x8 characters and fold duplicates.

    Matching is done against horizontal, vertical and both flips, since the
    tilemap carries a flip bit for each axis -- on ground art that is mostly
    symmetric, that folds out a good third of the characters.  The folding is
    identical for both targets; only the order entries are written in differs.

    layout="snes"      two 32x32 screens side by side, left screen first, which
                       is how the SNES PPU stores a 64x32 map.  Only valid at
                       exactly that size.
    layout="rowmajor"  plain rows, which is what the DS wants and what any map
                       larger than one SNES tilemap has to use.
    """
    cw, ch_ = world.w // 8, world.h // 8
    if layout == "snes" and (cw, ch_) != (TILEMAP_W, TILEMAP_H):
        raise SystemExit(f"snes layout needs a {TILEMAP_W}x{TILEMAP_H} character "
                         f"map, got {cw}x{ch_}: use layout='rowmajor'")

    chars: list[bytes] = []
    index: dict[bytes, tuple[int, int]] = {}
    entries: list[int] = [0] * (cw * ch_)

    for ty in range(ch_):
        for tx in range(cw):
            tile = [[world.px[ty * 8 + y][tx * 8 + x] for x in range(8)]
                    for y in range(8)]
            key = bytes(v for row in tile for v in row)

            hit = index.get(key)
            if hit is None:
                flip_h = bytes(v for row in tile for v in reversed(row))
                hit = index.get(flip_h)
                if hit is not None:
                    hit = (hit[0], hit[1] | 0x4000)
            if hit is None:
                flip_v = bytes(v for row in reversed(tile) for v in row)
                hit = index.get(flip_v)
                if hit is not None:
                    hit = (hit[0], hit[1] | 0x8000)
            if hit is None:
                flip_hv = bytes(v for row in reversed(tile) for v in reversed(row))
                hit = index.get(flip_hv)
                if hit is not None:
                    hit = (hit[0], hit[1] | 0xC000)

            if hit is None:
                num = len(chars)
                chars.append(tile_4bpp(tile))
                index[key] = (num, 0)
                hit = (num, 0)

            # A 64x32 SNES tilemap is NOT stored as 32 rows of 64 entries: the
            # PPU keeps it as two 32x32 screens laid side by side, the left
            # screen first.  Writing it row-major shreds the map into diagonal
            # bands.  The DS does not do this, and neither can any map too wide
            # for one SNES tilemap.
            if layout == "snes":
                screen = tx // 32
                entries[screen * 1024 + ty * 32 + (tx % 32)] = hit[0] | hit[1]
            else:
                entries[ty * cw + tx] = hit[0] | hit[1]

    tilemap = bytearray()
    for e in entries:
        tilemap += bytes((e & 0xFF, (e >> 8) & 0xFF))
    return b"".join(chars), bytes(tilemap), len(chars)


# ---------------------------------------------------------------------------
# Sora
# ---------------------------------------------------------------------------

OUT, SKIN_L, SKIN_M, SKIN_D = 1, 2, 3, 4
HAIR_L, HAIR_M = 5, 6
RED_L, RED_D = 7, 8
NAVY_L, NAVY_D = 9, 10
WHITE = 11
SHOE_L, SHOE_D = 12, 13
BLADE, GOLD = 14, 15

# Screen-space "forward" and "sideways" for each drawn facing.
#   0 = south (toward the camera), 1 = south-east, 2 = east,
#   3 = north-east, 4 = north (away)
FWD = {0: (0, 1), 1: (2, 1), 2: (3, 0), 3: (2, -1), 4: (0, -1)}
LAT = {0: (6, 0), 1: (6, -1), 2: (1, 3), 3: (6, 1), 4: (6, 0)}


def spike(c: Canvas, bx: float, by: float, tx: float, ty: float,
          halfw: float, colour: int) -> None:
    """A tapered hair spike: a triangle laid along base -> tip.

    Thickness is applied *perpendicular* to the axis.  Thickening along both
    axes instead (the obvious shortcut) fattens neighbouring spikes into each
    other and the crown turns into a brim.
    """
    dx, dy = tx - bx, ty - by
    length = max((dx * dx + dy * dy) ** 0.5, 1.0)
    px, py = -dy / length, dx / length          # unit perpendicular
    steps = max(int(length * 2), 2)
    for k in range(steps + 1):
        t = k / steps
        x, y = bx + dx * t, by + dy * t
        w = halfw * (1.0 - t)
        m = 0.0
        while m <= w + 0.35:
            c.set(round(x + px * m), round(y + py * m), colour)
            c.set(round(x - px * m), round(y - py * m), colour)
            m += 0.5


# Sora's silhouette at 32x32, as an explicit vertical budget measured from
# the soles up.  Fixing these first is what stops the hair from eating the
# face: the fringe is a hard line, and nothing hair-coloured crosses it.
SHOE_CY = 29
LEG_TOP = 23
TORSO_TOP = 18
TORSO_BOT = 25
HEAD_CY = 13
HAIR_LINE = 10                  # hair may only occupy rows <= this
SPIKE_ORIGIN_Y = 9

# Spike tips, relative to (centre, SPIKE_ORIGIN_Y).  Five long spikes read far
# better at this size than seven short ones, which just merge.
SPIKES = ((-11, -2), (-7, -7), (0, -9), (7, -7), (11, -2))


def sora_frame(facing: int, frame: int) -> Canvas:
    """One 32x32 cel.  Frames 0-3 walk, 4-5 swing the keyblade."""
    c = Canvas(32, 32)
    cx = 16
    attacking = frame >= 4
    walk = 0 if attacking else frame

    swing = [0, 1, 0, -1][walk]
    bob = [0, -1, 0, -1][walk]
    if attacking:
        bob = -1 if frame == 4 else 0

    fx, fy = FWD[facing]
    lx, ly = LAT[facing]
    back = facing == 4

    # ---- legs and the oversized shoes that carry the silhouette ----
    for sign in (1, -1):
        ox = (lx * sign) // 2 + (fx * swing * sign) // 2
        oy = (ly * sign) // 2 + (fy * swing * sign) // 2
        c.rect(cx + ox - 1, LEG_TOP + bob + oy, cx + ox + 1,
               SHOE_CY - 1 + bob + oy, NAVY_D)
        c.ellipse(cx + ox, SHOE_CY + bob + oy, 2.9, 2.0, SHOE_L)
        c.ellipse(cx + ox, SHOE_CY + 1 + bob + oy, 2.6, 1.2, SHOE_D)

    # ---- torso ----
    t0, t1 = TORSO_TOP + bob, TORSO_BOT + bob
    c.rect(cx - 4, t0, cx + 4, t1, RED_L)
    c.ellipse(cx, t1, 4.4, 2.2, RED_L)
    c.rect(cx + 3, t0, cx + 4, t1, RED_D)               # shaded flank
    if not back:
        c.rect(cx - 1, t0 + 2, cx + 1, t1 - 1, NAVY_L)  # centre panel
        c.hline(cx - 3, cx + 3, t0, WHITE)              # collar
    else:
        c.hline(cx - 3, cx + 3, t0, NAVY_L)

    # ---- arms, and the Keyblade, which Sora is never without ----
    arm_y = t0 + 3
    if attacking:
        if frame == 4:                  # wind-up: raised overhead
            hx, hy = cx + 5, arm_y - 4
            bdx, bdy = 0.1, -1.0
        else:                           # follow-through: out along the facing
            reach = 8
            hx = cx + (fx * reach) // 3
            hy = arm_y + (fy * reach) // 3
            bdx, bdy = float(fx or 1), float(fy)
        c.ellipse((cx + hx) // 2, (arm_y + hy) // 2, 1.8, 1.8, RED_L)
        c.ellipse(hx, hy, 1.7, 1.7, WHITE)              # gloved hand
        draw_keyblade(c, hx, hy, bdx, bdy, 14)
    else:
        for sign in (1, -1):
            ox = (lx * sign) // 2 - (fx * swing * sign) // 3
            oy = (ly * sign) // 2 - (fy * swing * sign) // 3
            c.ellipse(cx + ox, arm_y + oy, 1.6, 2.2, RED_L)
            c.ellipse(cx + ox, arm_y + 3 + oy, 1.5, 1.5, WHITE)
        # Resting grip: blade angled down and forward past his right side.
        hx = cx + (lx // 2) + 1
        hy = arm_y + 1 + (ly // 2)
        draw_keyblade(c, hx, hy, 0.9, 0.7, 10)

    # ---- head, drawn before the hair so the fringe lands on top of it ----
    c.ellipse(cx, HEAD_CY + bob, 5.4, 5.0, SKIN_L)
    c.ellipse(cx + 1, HEAD_CY + 1 + bob, 4.2, 3.8, SKIN_M)

    # ---- hair: cap plus spikes, both clipped above the fringe line ----
    hair = Canvas(32, 32)
    if back:
        hair.ellipse(cx, HEAD_CY + bob, 6.0, 5.4, HAIR_M)
        hair.ellipse(cx, HEAD_CY - 2 + bob, 4.6, 3.0, HAIR_L)
        clip = HEAD_CY + 5 + bob        # the back of the head is all hair
    else:
        hair.ellipse(cx, HAIR_LINE - 4 + bob, 6.0, 4.6, HAIR_M)
        hair.ellipse(cx, HAIR_LINE - 6 + bob, 4.4, 2.4, HAIR_L)
        # side locks reaching just past the fringe
        hair.rect(cx - 7, HAIR_LINE - 2 + bob, cx - 6, HAIR_LINE + 2 + bob, HAIR_M)
        hair.rect(cx + 6, HAIR_LINE - 2 + bob, cx + 7, HAIR_LINE + 2 + bob, HAIR_M)
        clip = HAIR_LINE + 2 + bob

    for sx, sy in SPIKES:
        bx = cx + sx // 3
        by = SPIKE_ORIGIN_Y + bob + sy // 3
        spike(hair, bx, by, cx + sx, SPIKE_ORIGIN_Y + bob + sy, 2, HAIR_M)
    for sx, sy in SPIKES:
        bx = cx + sx // 3
        by = SPIKE_ORIGIN_Y + bob + sy // 3
        spike(hair, bx, by, cx + sx, SPIKE_ORIGIN_Y + bob + sy, 1, HAIR_L)

    for y in range(clip + 1, 32):       # nothing hair-coloured below the fringe
        hair.px[y] = [0] * 32
    c.blit(hair, 0, 0)

    if not back:
        for side in (-1, 1):
            ex = cx + side * 3
            c.rect(ex - 1, HEAD_CY + bob, ex, HEAD_CY + 1 + bob, OUT)
            c.set(ex - 1, HEAD_CY + bob, WHITE)
        c.hline(cx - 1, cx + 1, HEAD_CY + 3 + bob, SKIN_D)

    c.outline(OUT)
    return c


def draw_keyblade(c: Canvas, hx: float, hy: float,
                  dx: float, dy: float, length: int) -> None:
    """The Keyblade, built along its own axis so it reads at any angle.

    The silhouette is the point of the whole weapon, so all four parts are
    drawn explicitly: a guard boxing the grip, a plain shaft, a key bit
    projecting off one side of the tip, and two teeth cut into that bit.
    """
    norm = max((dx * dx + dy * dy) ** 0.5, 1e-6)
    ux, uy = dx / norm, dy / norm       # along the blade
    vx, vy = -uy, ux                    # across it

    def put(u: float, v: float, colour: int) -> None:
        c.set(round(hx + ux * u + vx * v), round(hy + uy * u + vy * v), colour)

    # Grip below the hand, and a keychain token hanging off the pommel.
    for u in (-1.0, -2.0, -3.0):
        put(u, 0.0, NAVY_D)
    put(-4.0, 0.0, GOLD)

    # Guard: a bar across the blade plus two short arms up the shaft, which is
    # what makes the hilt read as a boxed key handle rather than a sword.
    for v in (-2.0, -1.5, -1.0, 1.0, 1.5, 2.0):
        put(1.0, v, GOLD)
    for u in (2.0, 2.5, 3.0):
        put(u, -2.0, GOLD)
        put(u, 2.0, GOLD)

    # Shaft.
    u = 3.0
    while u <= length - 4:
        put(u, -0.5, BLADE)
        put(u, 0.5, BLADE)
        u += 0.5

    # Key bit: a block projecting off one side of the tip...
    tip = float(length)
    u = tip - 3.0
    while u <= tip:
        for v in (0.0, 0.5, 1.0, 1.5, 2.0, 2.5):
            put(u, v, GOLD)
        put(u, -0.5, BLADE)
        u += 0.5

    # ...with a notch cut between two teeth.
    for v in (1.5, 2.0, 2.5):
        put(tip - 1.5, v, 0)
        put(tip - 1.0, v, 0)


def build_sora() -> Canvas:
    """6 frames across, 5 facings down."""
    sheet = Canvas(6 * 32, 5 * 32)
    for facing in range(5):
        for frame in range(6):
            sheet.blit(sora_frame(facing, frame), frame * 32, facing * 32)
    return sheet


def sora_stream_order(sheet: Canvas) -> bytes:
    """Serialise the sheet in the order the streaming DMA expects.

    The NMI uploads a frame as four 128-byte rows, so each 32x32 cel has to be
    stored as its own contiguous 512-byte run of 16 tiles in raster order.
    """
    out = bytearray()
    for facing in range(5):
        for frame in range(6):
            cel = sheet.sub(frame * 32, facing * 32, 32, 32)
            for row in range(4):
                for col in range(4):
                    tile = [[cel.px[row * 8 + y][col * 8 + x] for x in range(8)]
                            for y in range(8)]
                    out += tile_4bpp(tile)
    return bytes(out)


# ---------------------------------------------------------------------------
# Scenery, enemies and effects
# ---------------------------------------------------------------------------

S_OUT, LEAF_L, LEAF_M, LEAF_D = 1, 2, 3, 4
TRUNK_L, TRUNK_M, TRUNK_D = 5, 6, 7
STONE_L, STONE_M, STONE_D = 8, 9, 10
COCONUT, SPINE = 11, 12


def draw_palm() -> Canvas:
    c = Canvas(32, 32)
    # Trunk: leans, tapers, and darkens toward the base.
    for k in range(18):
        y = 31 - k
        x = 16 + (k * k) // 90
        w = 2 if k < 10 else 1
        c.rect(x - w, y, x + w, y, TRUNK_M)
        c.set(x - w, y, TRUNK_D)
        c.set(x + w, y, TRUNK_L)
    top_x, top_y = 16 + (17 * 17) // 90, 13

    # Fronds radiating from the crown.
    fronds = [(-11, 2), (-8, -4), (0, -6), (8, -4), (11, 2), (-5, 5), (5, 5)]
    for dx, dy in fronds:
        for t in range(9):
            px = top_x + (dx * t) // 8
            py = top_y + (dy * t) // 8 + (t * t) // 18
            shade = LEAF_L if dy < 0 else LEAF_M
            c.ellipse(px, py, 2.4 - t * 0.12, 1.9 - t * 0.10, shade)
        c.set(top_x + dx // 2, top_y + dy // 2, SPINE)
    c.ellipse(top_x, top_y, 3.2, 2.4, LEAF_D)
    c.set(top_x - 2, top_y + 2, COCONUT)
    c.set(top_x + 1, top_y + 3, COCONUT)
    c.outline(S_OUT)
    return c


def draw_boulder() -> Canvas:
    c = Canvas(32, 32)
    c.ellipse(16, 24, 11.0, 7.0, STONE_M)
    c.ellipse(14, 21, 8.0, 5.4, STONE_L)
    c.ellipse(19, 27, 8.0, 4.0, STONE_D)
    for x, y in ((10, 20), (21, 19), (13, 27), (24, 24)):
        c.set(x, y, STONE_D)
        c.set(x + 1, y, STONE_D)
    c.outline(S_OUT)
    return c


def draw_rock() -> Canvas:
    c = Canvas(16, 16)
    c.ellipse(8, 11, 6.0, 4.0, STONE_M)
    c.ellipse(7, 9, 4.2, 2.8, STONE_L)
    c.ellipse(10, 13, 4.0, 2.0, STONE_D)
    c.outline(S_OUT)
    return c


H_OUT, H_BODY, H_HI, H_EYE, H_EYE_RIM, H_ANT, H_UNDER = 1, 2, 3, 4, 5, 6, 7


# Which indices a Shadow is drawn against.  The Dive has a palette all to
# itself; on the night the island falls, OBJ palette 1 is shared with Riku and
# Kairi, so the same shapes are cut from three borrowed colours instead of six.
HEART_PAL = (H_OUT, H_BODY, H_HI, H_EYE, H_EYE_RIM, H_ANT, H_UNDER)
HEART_PAL_NIGHT = (1, 6, 7, 8, 8, 7, 1)


def draw_heartless(frame: int, pal: tuple[int, ...] = HEART_PAL) -> Canvas:
    """A Shadow: crouched, twitching antennae, two yellow eyes."""
    H_OUT, H_BODY, H_HI, H_EYE, H_EYE_RIM, H_ANT, H_UNDER = pal
    c = Canvas(16, 16)
    crouch = [0, 1, 0, -1][frame]
    by = 12 + crouch

    c.ellipse(8, by - 2, 5.2, 4.0, H_BODY)
    c.ellipse(8, by, 5.0, 2.4, H_UNDER)
    c.ellipse(6, by - 4, 3.0, 2.0, H_HI)

    # Antennae sway with the frame.
    sway = [0, 1, 0, -1][frame]
    for side in (-1, 1):
        ax = 8 + side * 2
        for k in range(5):
            c.set(ax + side * (k // 2) + (sway if k > 2 else 0), by - 6 - k, H_ANT)

    # Eyes.
    for side in (-1, 1):
        ex = 8 + side * 2
        c.rect(ex - 1, by - 4, ex, by - 3, H_EYE)
        c.set(ex - 1, by - 4, H_EYE_RIM)

    # Clawed feet.
    c.set(4, by + 2, H_BODY)
    c.set(11, by + 2, H_BODY)
    c.outline(H_OUT)
    return c


def draw_shadow_blob() -> Canvas:
    """Drawn in the colour-math palette, so this reads as 50% darkening."""
    c = Canvas(16, 16)
    c.ellipse(8, 12, 7.0, 3.4, 1)
    return c


def draw_shadow_big() -> Canvas:
    """Large actors need a blob wide enough to show around their own sprite."""
    c = Canvas(32, 32)
    c.ellipse(16, 24, 13.0, 6.0, 1)
    return c


def draw_slash(frame: int) -> Canvas:
    """A crescent arc for the keyblade swing."""
    c = Canvas(16, 16)
    radius = 6.5 if frame == 0 else 7.5
    for t in range(-7, 8):
        ang = t / 9.0
        x = 8 + int(radius * (1 - ang * ang * 0.55))
        y = 8 + int(radius * ang)
        c.set(x, y, 1)
        c.set(x - 1, y, 2)
        c.set(x - 2, y, 3 if frame == 0 else 4)
    return c


# ---------------------------------------------------------------------------
# Station of Awakening -- the stained-glass platform the game opens on
# ---------------------------------------------------------------------------

import math                                             # noqa: E402

DIVE_CX, DIVE_CY = 256.0, 128.0
# Seen from overhead the station is a circle, not an ellipse, so the radius is
# capped by the shorter axis of the world.  The SNES cannot pull the camera
# back, so this is as large as the platform can be and still be read as round.
DIVE_RX = DIVE_RY = 110.0

# ...and the DS screen is 32 lines shorter, which this does not survive.  A
# 220 px disc fits the SNES's 224 lines by two pixels at each end; on 192 it is
# clipped by 14 top and bottom, and a Station of Awakening with its golden rim
# cut off is not a station.  So the DS draws a smaller one.  The SNES keeps 110
# and its ROM stays byte-identical.  See
# docs/behaviour/divergences/004-ds-station-radius.md.
DS_DIVE_R = 92.0

# How far inside the rim a tile centre has to sit to be standable.  Absolute
# rather than proportional: it is a margin for the sprite's feet, not a fraction
# of the disc.
DIVE_INSET = 14.0

# Glass palette indices, named.
V_VOID, G_DEEP, G_MID, G_LIGHT = 0, 1, 2, 3
G_GOLD, G_GOLD_D, G_RED, G_RED_D = 4, 5, 6, 7
G_PALE, G_SKIN, G_HAIR, G_WHITE = 8, 9, 10, 11
G_GREEN, G_VIOLET, G_TEAL, G_EDGE = 12, 13, 14, 15

WEDGE_COLOURS = (G_MID, G_DEEP, G_TEAL, G_DEEP, G_VIOLET, G_DEEP,
                 G_MID, G_DEEP, G_TEAL, G_DEEP, G_VIOLET, G_DEEP)

# The second Station of Awakening: cooler glass, a fair-haired figure in blue.
WEDGE_COLOURS_2 = (G_TEAL, G_DEEP, G_LIGHT, G_DEEP, G_MID, G_DEEP,
                   G_TEAL, G_DEEP, G_LIGHT, G_DEEP, G_MID, G_DEEP)

# The third: warmer, rose and violet, for the platform the boss rises on.
WEDGE_COLOURS_3 = (G_RED, G_DEEP, G_VIOLET, G_DEEP, G_GOLD_D, G_DEEP,
                   G_RED, G_DEEP, G_VIOLET, G_DEEP, G_GOLD_D, G_DEEP)


def dive_medallion(r: float, ang: float, nx: float, ny: float,
                   station: int = 1) -> int | None:
    """The figure at the centre of the platform.

    nx/ny run -1..1 across the medallion, so the drawing below is composed as
    if seen head-on and is laid flat onto the glass, which is how the stations
    read in the source game.
    """
    # Pale radiating backdrop.
    base = G_PALE if int((ang / (2 * math.pi)) * 24) % 2 == 0 else G_WHITE
    # Station two dresses the same figure differently: fair hair, blue gown.
    if station == 1:
        hair, hair_hi, bow = G_HAIR, G_HAIR, G_RED
        bodice_a, bodice_b = G_MID, G_DEEP
        skirt_a, skirt_b, sleeve = G_PALE, G_GOLD, G_RED_D
    elif station == 2:
        hair, hair_hi, bow = G_GOLD, G_PALE, G_LIGHT
        bodice_a, bodice_b = G_LIGHT, G_MID
        skirt_a, skirt_b, sleeve = G_WHITE, G_LIGHT, G_WHITE
    else:
        hair, hair_hi, bow = G_PALE, G_WHITE, G_RED_D
        bodice_a, bodice_b = G_RED, G_RED_D
        skirt_a, skirt_b, sleeve = G_PALE, G_RED, G_WHITE

    # --- head: an elliptical face set inside a rounder mass of hair ---
    hx, hy = nx, ny + 0.42
    if hx * hx + hy * hy < 0.32 * 0.32:
        fx, fy = nx / 0.19, (ny + 0.40) / 0.23
        if fx * fx + fy * fy < 1.0:
            return G_SKIN
        if hy < -0.16:
            return hair_hi
        return hair
    # bow above the hair
    bx, by = nx, ny + 0.76
    if bx * bx * 0.7 + by * by < 0.16 * 0.16:
        return bow

    # --- bodice ---
    if -0.20 < ny < 0.16 and abs(nx) < 0.26 - ny * 0.25:
        return bodice_a if abs(nx) < 0.18 else bodice_b
    # collar
    if 0.10 < ny + 0.22 < 0.20 and abs(nx) < 0.30:
        return G_WHITE

    # --- skirt, widening toward the bottom ---
    if 0.16 <= ny < 0.86:
        span = 0.20 + (ny - 0.16) * 0.62
        if abs(nx) < span:
            # gold pleats
            if int(abs(nx) / span * 7) % 2 == 0:
                return skirt_a
            return skirt_b
    # sleeves
    for side in (-1.0, 1.0):
        sx, sy = nx - side * 0.30, ny + 0.02
        if sx * sx + sy * sy < 0.13 * 0.13:
            return sleeve
    return base


def build_dive_platform(station: int = 1,
                        radius: float = DIVE_RX) -> tuple[Canvas, bytes]:
    """Paint the platform and derive which ground tiles are standable.

    The radius is a parameter because the DS screen is 32 lines shorter and the
    SNES's disc does not fit on it.  Defaulted, so the SNES call sites are
    unchanged and its ROM stays byte-identical.
    """
    world = Canvas(WORLD_W, WORLD_H, V_VOID)

    for y in range(WORLD_H):
        for x in range(WORLD_W):
            dx = (x + 0.5 - DIVE_CX) / radius
            dy = (y + 0.5 - DIVE_CY) / radius
            r = math.sqrt(dx * dx + dy * dy)
            if r > 1.0:
                continue
            ang = math.atan2(dy, dx) + math.pi

            if r > 0.965:
                c = G_GOLD_D                        # outer lip
            elif r > 0.935:
                c = G_GOLD
            elif r > 0.83:
                # dark band ticked with gold spokes every 15 degrees
                c = G_GOLD if int(ang / (math.pi / 12)) % 2 == 0 else G_DEEP
            elif r > 0.80:
                c = G_GOLD
            elif r > 0.60:
                # ring of stained-glass wedges
                wedges = (WEDGE_COLOURS if station == 1 else
                          WEDGE_COLOURS_2 if station == 2 else
                          WEDGE_COLOURS_3)
                seg = int(ang / (2 * math.pi) * len(wedges))
                c = wedges[seg % len(wedges)]
                if abs((ang % (2 * math.pi / len(wedges)))) < 0.035:
                    c = G_GOLD_D                    # leading between wedges
            elif r > 0.565:
                c = G_GOLD
            else:
                nx = dx / 0.565
                ny = dy / 0.565
                c = dive_medallion(r, ang, nx, ny, station) or G_PALE
            world.set(x, y, c)

    # Standable tiles: those whose centre sits comfortably inside the rim.
    coll = bytearray(MAP_W * MAP_H)
    for j in range(MAP_H):
        for i in range(MAP_W):
            wx = i * TILE + TILE // 2
            wy = j * TILE + TILE // 2
            dx = (wx - DIVE_CX) / (radius - DIVE_INSET)
            dy = (wy - DIVE_CY) / (radius - DIVE_INSET)
            coll[j * MAP_W + i] = 1 if dx * dx + dy * dy <= 1.0 else 0
    return world, bytes(coll)


# --- pedestals and the three dream weapons --------------------------------

D_OUT, D_STONE_L, D_STONE_M, D_STONE_D = 1, 2, 3, 4
D_MET_L, D_MET_M, D_MET_D = 5, 6, 7
D_GOLD, D_GOLD_D, D_RED, D_RED_D = 8, 9, 10, 11
D_BLUE, D_BLUE_D, D_GLOW, D_WOOD = 12, 13, 14, 15


def draw_pedestal() -> Canvas:
    """A short dais for a weapon to hover over."""
    c = Canvas(32, 32)
    # column
    c.rect(10, 18, 21, 27, D_STONE_D)
    c.rect(10, 18, 15, 27, D_STONE_M)
    # top slab, an ellipse -- a cylinder seen from three-quarters above
    for y in range(10):
        hw = 2 * (y + 1) if y < 5 else 2 * (10 - y)
        for x in range(16 - hw, 16 + hw):
            c.set(x, 13 + y, D_STONE_L if y < 5 else D_STONE_M)
    # base
    for y in range(6):
        hw = 2 * (y + 1) if y < 3 else 2 * (6 - y)
        for x in range(16 - hw, 16 + hw):
            c.set(x, 25 + y, D_STONE_M if y < 3 else D_STONE_D)
    c.outline(D_OUT)
    return c


def draw_sword() -> Canvas:
    """Dream Sword: a broad straight blade with a gold crossguard."""
    c = Canvas(32, 32)
    c.rect(14, 3, 17, 20, D_MET_L)          # blade
    c.rect(16, 3, 17, 20, D_MET_M)          # shaded edge
    c.set(15, 2, D_MET_L)
    c.set(16, 2, D_MET_L)
    c.rect(10, 20, 21, 22, D_GOLD)          # crossguard
    c.rect(10, 22, 21, 22, D_GOLD_D)
    c.rect(14, 23, 17, 28, D_RED)           # grip
    c.rect(16, 23, 17, 28, D_RED_D)
    c.ellipse(16, 29, 2.6, 2.0, D_GOLD)     # pommel
    c.outline(D_OUT)
    return c


def draw_shield() -> Canvas:
    """Dream Shield: a rounded shield carrying the three-circle emblem."""
    c = Canvas(32, 32)
    c.ellipse(16, 15, 11.0, 12.0, D_BLUE)
    c.rect(5, 4, 27, 15, D_BLUE)
    c.ellipse(16, 15, 8.6, 9.8, D_BLUE_D)
    c.rect(8, 6, 24, 15, D_BLUE_D)
    # rim
    c.ellipse(16, 15, 11.0, 12.0, D_GOLD)
    c.ellipse(16, 15, 9.4, 10.4, D_BLUE)
    c.rect(6, 4, 26, 6, D_GOLD)
    # emblem: one large circle and two ears
    c.ellipse(16, 17, 5.0, 5.0, D_RED)
    c.ellipse(11, 10, 3.0, 3.0, D_RED)
    c.ellipse(21, 10, 3.0, 3.0, D_RED)
    c.ellipse(16, 17, 3.4, 3.4, D_RED_D)
    c.outline(D_OUT)
    return c


def draw_staff() -> Canvas:
    """Dream Rod: a slim rod topped with the same three-circle emblem."""
    c = Canvas(32, 32)
    c.rect(15, 10, 17, 30, D_WOOD)          # shaft
    c.rect(15, 10, 15, 30, D_GOLD_D)
    c.ellipse(16, 8, 5.2, 5.2, D_BLUE)      # emblem head
    c.ellipse(11, 3, 3.0, 3.0, D_BLUE)
    c.ellipse(21, 3, 3.0, 3.0, D_BLUE)
    c.ellipse(16, 8, 3.4, 3.4, D_BLUE_D)
    c.ellipse(16, 12, 2.4, 1.6, D_GOLD)     # collar
    c.outline(D_OUT)
    return c


def draw_darkside() -> Canvas:
    """Darkside, 64x64 -- four 32x32 sprites assembled by the OAM builder.

    Twice Sora in both axes is as large as a single sprite can get without
    surrendering the 16x16 size slot the Shadows need, so the silhouette does
    the work: long limbs, a tiny head set low between huge shoulders, and the
    heart-shaped hole punched clean through the chest.
    """
    c = Canvas(64, 64)
    B_OUT, B_BODY, B_HI, B_EYE, B_EYERIM = 1, 2, 3, 4, 5
    B_SINEW, B_RIM, B_HOLE, B_GLOW = 6, 8, 9, 10
    B_MID, B_DEEP, B_LIGHT = 11, 12, 13

    # Built as one connected mass rather than separate limbs: at 64x64 against
    # bright glass the silhouette is the whole read, and detached arms just
    # look like pillars standing behind the body.

    # --- legs ---
    c.rect(22, 44, 29, 58, B_DEEP)
    c.rect(35, 44, 42, 58, B_DEEP)
    c.ellipse(25, 59, 7.0, 3.4, B_BODY)
    c.ellipse(39, 59, 7.0, 3.4, B_BODY)

    # --- shoulders sweeping out, then the torso tapering to the waist ---
    for y in range(21, 34):
        t = (y - 21) / 13.0
        hw = int(25 - 6 * t)
        c.rect(32 - hw, y, 32 + hw, y, B_MID)
    for y in range(34, 48):
        t = (y - 34) / 14.0
        hw = int(19 - 5 * t)
        c.rect(32 - hw, y, 32 + hw, y, B_MID)

    # --- arms: continuous from the shoulder tips down to the hands ---
    for side in (-1, 1):
        for y in range(24, 50):
            t = (y - 24) / 26.0
            ax = 32 + side * int(22 + 3 * t)
            c.rect(ax - 5, y, ax + 5, y, B_BODY)
        hx = 32 + side * 25
        c.ellipse(hx, 53, 7.0, 6.0, B_BODY)
        c.ellipse(hx - side, 51, 4.4, 3.6, B_HI)
        for k in range(3):                          # splayed fingers
            c.rect(hx - 5 + k * 4, 57, hx - 4 + k * 4, 60, B_BODY)

    # Seams: without them the arms and legs merge into the torso and the whole
    # figure reads as one slab.
    for side in (-1, 1):
        for y in range(26, 50):
            t = (y - 26) / 24.0
            ax = 32 + side * int(22 + 3 * t)
            c.vline(ax - side * 6, y, y, B_OUT)
    c.vline(32, 44, 58, B_OUT)                      # between the legs
    c.ellipse(24, 34, 8.0, 12.0, B_DEEP)            # shaded flank

    # --- the heart-shaped hole, punched clean through the chest ---
    for y in range(28, 50):
        for x in range(20, 45):
            fx = (x + 0.5 - 32) / 8.0
            fy = -(y + 0.5 - 36) / 8.5
            t = fx * fx + fy * fy - 1.0
            if t * t * t - fx * fx * fy * fy * fy <= 0.0:
                c.set(x, y, B_HOLE)
    for y in range(28, 50):
        for x in range(20, 45):
            if c.get(x, y) != B_HOLE:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                if c.get(x + dx, y + dy) not in (B_HOLE, B_GLOW):
                    c.set(x, y, B_GLOW)
                    break

    # --- head, sitting proud of the shoulder line ---
    c.rect(28, 18, 36, 24, B_BODY)                  # neck into the chest
    c.ellipse(32, 14, 9.0, 8.0, B_BODY)
    c.ellipse(30, 12, 5.5, 4.2, B_HI)
    for side in (-1, 1):
        ex = 32 + side * 4
        c.ellipse(ex, 15, 2.9, 2.2, B_EYERIM)
        c.ellipse(ex, 15, 1.9, 1.4, B_EYE)

    # --- tendrils, swept back off the crown and kept clear of the face ---
    TENDRILS = ((-18, -1), (-12, -6), (-5, -8), (4, -8), (12, -5), (18, 0))
    for dx, dy in TENDRILS:
        spike(c, 32 + dx // 3, 7 + dy // 3, 32 + dx, max(0, 7 + dy), 2.2, B_SINEW)
    for dx, dy in TENDRILS:
        spike(c, 32 + dx // 3, 7 + dy // 3, 32 + dx, max(0, 7 + dy), 0.9, B_LIGHT)

    # Catch the upward edges before the outline goes on, leaving the eyes and
    # the chest cavity alone.
    c.rim_light(B_RIM, skip=(B_EYE, B_EYERIM, B_HOLE, B_GLOW))

    c.outline(B_OUT)
    return c


def draw_orb(frame: int) -> Canvas:
    """A dark orb spat from the heart-shaped hole: dark core, glowing rim."""
    c = Canvas(16, 16)
    B_OUT, B_HOLE, B_GLOW, B_DEEP = 1, 9, 10, 12
    r = 6.2 if frame == 0 else 5.4
    c.ellipse(8, 8, r, r, B_GLOW)
    c.ellipse(8, 8, r - 1.4, r - 1.4, B_HOLE)
    c.ellipse(8, 8, r - 3.0, r - 3.0, B_DEEP)
    c.set(6, 6, B_GLOW)                         # a catchlight so it reads round
    c.outline(B_OUT)
    return c


def draw_streak(frame: int) -> Canvas:
    """A rising mote of light, to give the fall something to measure against."""
    c = Canvas(16, 16)
    F_CORE, F_IN, F_MID, F_OUT = 1, 2, 3, 4
    h = 6 if frame == 0 else 4
    c.rect(7, 8 - h, 8, 8 + h, F_MID)
    c.rect(7, 8 - h + 2, 8, 8 + h - 2, F_IN)
    c.vline(7, 8 - h + 4, 8 + h - 4, F_CORE)
    c.set(6, 8, F_OUT)
    c.set(9, 8, F_OUT)
    return c


# ---------------------------------------------------------------------------
# The islanders, and the Day 1 raft materials
#
# These live on the second 256-tile sprite page, reached through the OAM name
# bit; the first page is full.  All five characters share one palette, so the
# hair colours are what tell them apart at a glance -- which is roughly how
# they read on screen at this size anyway.
# ---------------------------------------------------------------------------

I_OUT, I_SKIN_L, I_SKIN_M = 1, 2, 3
I_KAIRI, I_RIKU, I_TIDUS, I_SELPHIE, I_WAKKA = 4, 5, 6, 7, 8
I_WHITE, I_BLUE, I_YELLOW, I_PURPLE, I_NAVY, I_GREEN, I_RED = 9, 10, 11, 12, 13, 14, 15


def islander(hair: int, top: int, bottom: int, *, style: str,
             trim: int | None = None, prop: str | None = None,
             prop_col: int = I_WHITE) -> Canvas:
    """One 32x32 standing cel, on the same proportions as Sora's."""
    c = Canvas(32, 32)
    cx = 16

    # ---- legs ----
    for sign in (1, -1):
        ox = 3 * sign
        c.rect(cx + ox - 1, LEG_TOP, cx + ox + 1, SHOE_CY - 1, bottom)
        c.ellipse(cx + ox, SHOE_CY, 2.6, 1.8, I_WHITE)
        c.ellipse(cx + ox, SHOE_CY + 1, 2.4, 1.0, I_OUT)

    # ---- torso ----
    t0, t1 = TORSO_TOP, TORSO_BOT
    c.rect(cx - 4, t0, cx + 4, t1, top)
    c.ellipse(cx, t1, 4.4, 2.2, top)
    if trim is not None:
        c.hline(cx - 4, cx + 4, t1 - 1, trim)
        c.hline(cx - 3, cx + 3, t0, trim)

    # ---- arms ----
    arm_y = t0 + 3
    for sign in (1, -1):
        c.ellipse(cx + sign * 5, arm_y, 1.6, 2.4, top)
        c.ellipse(cx + sign * 5, arm_y + 3, 1.5, 1.5, I_SKIN_L)

    # ---- head ----
    c.ellipse(cx, HEAD_CY, 5.4, 5.0, I_SKIN_L)
    c.ellipse(cx + 1, HEAD_CY + 1, 4.2, 3.8, I_SKIN_M)

    # ---- hair, clipped above the fringe so it never covers the face ----
    h = Canvas(32, 32)
    h.ellipse(cx, HAIR_LINE - 4, 6.0, 4.8, hair)
    clip = HAIR_LINE + 1
    if style == "bob":                      # Kairi: a short bob to the jaw
        h.rect(cx - 7, HAIR_LINE - 3, cx - 5, HEAD_CY + 3, hair)
        h.rect(cx + 5, HAIR_LINE - 3, cx + 7, HEAD_CY + 3, hair)
        h.ellipse(cx, HAIR_LINE - 6, 4.6, 2.4, hair)
    elif style == "long":                   # Riku: past the shoulders
        h.rect(cx - 7, HAIR_LINE - 3, cx - 5, TORSO_TOP + 2, hair)
        h.rect(cx + 5, HAIR_LINE - 3, cx + 7, TORSO_TOP + 2, hair)
        h.hline(cx - 5, cx + 5, HAIR_LINE - 8, hair)
    elif style == "spiky":                  # Tidus: a blond mop
        for sx, sy in ((-8, -3), (-4, -6), (1, -7), (5, -5), (9, -2)):
            spike(h, cx + sx // 2, HAIR_LINE - 5, cx + sx, HAIR_LINE - 5 + sy,
                  2, hair)
    elif style == "flip":                   # Selphie: flicked-out ends
        h.rect(cx - 8, HAIR_LINE - 2, cx - 6, HEAD_CY + 1, hair)
        h.rect(cx + 6, HAIR_LINE - 2, cx + 8, HEAD_CY + 1, hair)
        h.hline(cx - 9, cx - 6, HEAD_CY + 2, hair)
        h.hline(cx + 6, cx + 9, HEAD_CY + 2, hair)
        clip = HEAD_CY + 2
    elif style == "up":                     # Wakka: swept straight up
        for k in range(9):                  # a flame, narrowing as it rises
            w = 4 - k // 3
            h.hline(cx - w, cx + w - 1 + (k % 2), HAIR_LINE - 5 - k, hair)
    if style == "bob":
        clip = HEAD_CY + 3
    elif style == "long":
        clip = TORSO_TOP + 2
    for y in range(clip + 1, 32):
        h.px[y] = [0] * 32
    c.blit(h, 0, 0)

    # ---- eyes ----
    for side in (-1, 1):
        ex = cx + side * 3
        c.rect(ex - 1, HEAD_CY, ex, HEAD_CY + 1, I_OUT)

    # ---- whatever they are holding ----
    hx, hy = cx + 6, arm_y + 3
    if prop == "sword":                     # Tidus' wooden practice blade
        for k in range(9):
            c.set(hx + 1 + k // 2, hy - 1 - k, prop_col)
            c.set(hx + 2 + k // 2, hy - 1 - k, prop_col)
    elif prop == "rope":                    # Selphie's skipping rope
        c.ellipse(hx + 3, hy + 3, 3.2, 3.6, prop_col)
        c.ellipse(hx + 3, hy + 3, 1.8, 2.2, 0)
    elif prop == "ball":                    # Wakka's blitzball
        c.ellipse(hx + 3, hy, 3.2, 3.2, prop_col)
        c.ellipse(hx + 2, hy - 1, 1.4, 1.4, I_WHITE)

    c.outline(I_OUT)
    return c


def draw_log() -> Canvas:
    """A length of driftwood, lying on its side."""
    c = Canvas(16, 16)
    W_L, W_M, W_D = I_YELLOW, I_SELPHIE, I_OUT
    c.ellipse(8, 10, 6.4, 2.6, I_SELPHIE)
    c.rect(2, 8, 13, 11, I_SELPHIE)
    c.hline(3, 12, 8, W_L)                  # sunlit upper edge
    c.hline(3, 12, 11, W_D)
    c.ellipse(3, 10, 1.6, 2.4, W_L)         # the sawn end grain
    c.ellipse(3, 10, 0.8, 1.2, W_D)
    c.outline(I_OUT)
    return c


def draw_cloth() -> Canvas:
    """A folded bolt of sailcloth."""
    c = Canvas(16, 16)
    c.rect(3, 7, 12, 12, I_WHITE)
    c.hline(3, 12, 9, I_SKIN_M)             # the fold
    c.hline(4, 11, 6, I_WHITE)
    c.set(3, 6, I_WHITE)
    c.set(12, 6, I_WHITE)
    c.hline(3, 12, 12, I_SKIN_M)
    c.outline(I_OUT)
    return c


def draw_rope() -> Canvas:
    """A coil of rope."""
    c = Canvas(16, 16)
    c.ellipse(8, 9, 5.4, 4.0, I_YELLOW)
    c.ellipse(8, 9, 2.6, 1.8, 0)            # the eye of the coil
    for k in range(0, 12, 3):               # the lay of the strands
        c.set(3 + k, 5 + (k % 2), I_SELPHIE)
        c.set(3 + k, 12 - (k % 2), I_SELPHIE)
    c.outline(I_OUT)
    return c


def draw_fish(frame: int) -> Canvas:
    """A little reef fish, two frames of tail."""
    c = Canvas(16, 16)
    flick = 1 if frame else -1
    c.ellipse(7, 8, 4.4, 2.8, I_BLUE)
    c.ellipse(6, 7, 3.0, 1.6, I_WHITE)
    c.ellipse(9, 8, 2.0, 1.6, I_YELLOW)             # flank stripe
    for k in range(3):                              # tail
        c.vline(12 + k, 8 - 1 - k, 8 + 1 + k, I_BLUE)
    c.set(13, 8 + 2 * flick, I_YELLOW)
    c.set(3, 7, I_OUT)                              # eye
    c.outline(I_OUT)
    return c


def draw_mushroom() -> Canvas:
    """Red cap, white spots, the sort that grows in a hollow."""
    c = Canvas(16, 16)
    c.rect(6, 9, 9, 13, I_WHITE)
    c.ellipse(8, 9, 5.4, 3.4, I_RED)
    c.ellipse(8, 8, 4.0, 2.2, I_RED)
    for (sx, sy) in ((5, 8), (9, 7), (11, 9), (7, 10)):
        c.set(sx, sy, I_WHITE)
        c.set(sx + 1, sy, I_WHITE)
    c.outline(I_OUT)
    return c


def draw_coconut() -> Canvas:
    """One of the gold ones, knocked out of a palm."""
    c = Canvas(16, 16)
    c.ellipse(8, 9, 4.4, 4.0, I_SELPHIE)
    c.ellipse(7, 8, 3.0, 2.6, I_YELLOW)
    for (sx, sy) in ((6, 7), (10, 8), (8, 11)):     # the three eyes
        c.set(sx, sy, I_OUT)
    c.outline(I_OUT)
    return c


def draw_egg() -> Canvas:
    """A seagull egg, sitting in what is left of the nest."""
    c = Canvas(16, 16)
    c.ellipse(8, 11, 6.0, 2.0, I_SELPHIE)           # the nest
    c.ellipse(8, 8, 3.6, 4.4, I_WHITE)
    c.ellipse(7, 7, 2.2, 2.8, I_SKIN_L)
    for (sx, sy) in ((6, 6), (9, 8), (7, 10), (10, 5)):
        c.set(sx, sy, I_SKIN_M)
    c.outline(I_OUT)
    return c


def draw_bottle() -> Canvas:
    """The bottle, full of water from under the fall."""
    c = Canvas(16, 16)
    c.rect(6, 6, 9, 13, I_BLUE)
    c.rect(7, 2, 8, 5, I_WHITE)                     # neck
    c.rect(6, 6, 6, 13, I_WHITE)                    # a highlight down one side
    c.hline(6, 9, 5, I_WHITE)                       # shoulder
    c.hline(6, 9, 13, I_NAVY)
    c.outline(I_OUT)
    return c


def draw_door() -> Canvas:
    """The door at the back of the Secret Place.  No handle, no keyhole.

    Hung flat on the cliff face, so what has to read at a glance is "door" and
    not "furniture": everything runs vertically, the surround is the same stone
    as the wall it is set into, and the only horizontals are the two rails.
    """
    c = Canvas(32, 32)
    WOOD, DARK, RAIL, STONE = I_SELPHIE, I_OUT, I_YELLOW, I_RIKU

    # A stone surround, arched over the top, set into the rock.
    c.rect(5, 5, 26, 31, STONE)
    c.ellipse(16, 6, 11.0, 5.0, STONE)
    c.rect(7, 7, 24, 31, DARK)              # the recess it sits in
    c.ellipse(16, 8, 9.0, 4.0, DARK)

    # The door: boards standing on end.
    c.rect(8, 9, 23, 31, WOOD)
    c.ellipse(16, 9, 8.0, 3.5, WOOD)
    for x in range(11, 23, 4):
        c.vline(x, 8, 31, DARK)             # the seams between boards
        c.vline(x + 1, 8, 31, RAIL)
    # Two cross rails, and nothing whatever where a handle would be.
    c.hline(8, 23, 15, RAIL)
    c.hline(8, 23, 16, DARK)
    c.hline(8, 23, 25, RAIL)
    c.hline(8, 23, 26, DARK)
    c.outline(I_OUT)
    return c


def draw_door_open() -> Canvas:
    """The same door, standing open.  What is behind it is not a room."""
    c = Canvas(32, 32)
    STONE, RECESS, DEEP, GLOW = I_RIKU, I_OUT, I_NAVY, I_PURPLE

    c.rect(5, 5, 26, 31, STONE)
    c.ellipse(16, 6, 11.0, 5.0, STONE)
    c.rect(7, 7, 24, 31, RECESS)
    c.ellipse(16, 8, 9.0, 4.0, RECESS)

    # The opening: dark that has depth to it rather than a flat hole.
    c.rect(9, 10, 22, 31, DEEP)
    c.ellipse(16, 10, 7.0, 3.0, DEEP)
    c.rect(12, 14, 19, 31, RECESS)
    for k, y in enumerate(range(12, 32, 3)):
        w = 6 - k // 2
        c.hline(16 - w, 16 + w - 1, y, GLOW if k % 2 else DEEP)
    # ...and it is coming out.
    for (gx, gy) in ((11, 13), (20, 17), (13, 22), (19, 26), (16, 30)):
        c.set(gx, gy, GLOW)
    c.outline(I_OUT)
    return c


def draw_dark_pool(frame: int) -> Canvas:
    """A column of darkness standing on the ground, two cels of it.

    This is what takes Riku, and later what comes out of the door after Kairi.
    """
    c = Canvas(32, 32)
    sway = 2 if frame else -2
    # The pool it stands in.
    c.ellipse(16, 29, 11.0, 3.4, I_NAVY)
    c.ellipse(16, 29, 8.0, 2.2, I_OUT)
    # The column, narrowing as it rises and leaning with the cel.
    for y in range(6, 29):
        t = (28 - y) / 22.0                      # 1 at the base, 0 at the top
        half = int(3 + 6 * t)
        lean = int(sway * (1.0 - t))
        c.hline(16 + lean - half, 16 + lean + half - 1, y, I_OUT)
        if half > 3:
            c.hline(16 + lean - half + 2, 16 + lean + half - 3, y, I_NAVY)
    # Violet edges, so it is not just a silhouette.
    for k, y in enumerate(range(7, 28, 3)):
        t = (28 - y) / 22.0
        half = int(3 + 6 * t)
        lean = int(sway * (1.0 - t))
        col = I_PURPLE if (k + frame) % 2 == 0 else I_NAVY
        c.set(16 + lean - half, y, col)
        c.set(16 + lean + half - 1, y, col)
    # Wisps torn off the top.
    for (wx, wy) in (((13, 5), (20, 8), (16, 2)) if frame else
                     ((19, 5), (12, 8), (17, 2))):
        c.set(wx + sway // 2, wy, I_PURPLE)
    return c


def chalk(c: Canvas, pts, col: int = I_WHITE) -> None:
    """Join a run of points with straight chalk strokes."""
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        steps = max(abs(x1 - x0), abs(y1 - y0)) or 1
        for k in range(steps + 1):
            c.set(x0 + (x1 - x0) * k // steps, y0 + (y1 - y0) * k // steps, col)


def draw_faces() -> Canvas:
    """The one that matters: two heads in profile, facing each other."""
    c = Canvas(32, 32)
    for cx, flip in ((9, 1), (22, -1)):
        # skull and jaw
        c.ellipse(cx, 14, 4.6, 5.2, 0)
        chalk(c, [(cx - 4 * flip, 12), (cx - 3 * flip, 8), (cx, 7),
                  (cx + 3 * flip, 9), (cx + 4 * flip, 13),
                  (cx + 3 * flip, 17), (cx, 19), (cx - 3 * flip, 17),
                  (cx - 4 * flip, 12)])
        c.set(cx + 2 * flip, 12, I_WHITE)               # eye
        chalk(c, [(cx + 3 * flip, 15), (cx + 2 * flip, 16)])   # mouth
    # a scratched line joining them, the way children do
    chalk(c, [(13, 22), (16, 24), (19, 22)])
    c.set(16, 26, I_WHITE)
    return c


def draw_scribbles() -> Canvas:
    """Years of smaller drawings, layered on top of each other."""
    c = Canvas(32, 32)
    # a boat with a sail
    chalk(c, [(3, 20), (13, 20), (11, 24), (5, 24), (3, 20)])
    chalk(c, [(8, 20), (8, 9), (14, 17), (8, 17)])
    # a star
    for (a, b) in (((22, 5), (25, 13)), ((25, 13), (18, 9)),
                   ((18, 9), (26, 9)), ((26, 9), (19, 13)), ((19, 13), (22, 5))):
        chalk(c, [a, b])
    # a fish
    chalk(c, [(19, 22), (24, 20), (28, 23), (24, 26), (19, 22)])
    chalk(c, [(28, 23), (30, 20), (30, 26), (28, 23)])
    # and a stick figure, small, low down
    chalk(c, [(6, 29), (6, 26)])
    chalk(c, [(3, 27), (9, 27)])
    c.set(6, 25, I_WHITE)
    return c


# ---------------------------------------------------------------------------
# Traverse Town
#
# The town's cast shares OBJ palette 1 with the Heartless, exactly as the night
# does, so the index names below are the island's read a second way: 4 is
# Goofy's fur rather than Kairi's hair, 5 is brown rather than Riku's silver.
# ---------------------------------------------------------------------------

T_TAN, T_BROWN = 4, 5


def draw_lamp() -> Canvas:
    """A street lamp.  A sprite rather than a tile so it stands up, and so
    somebody can walk behind it."""
    c = Canvas(32, 32)
    cx = 16
    c.rect(cx - 1, 10, cx + 1, 30, T_BROWN)
    c.vline(cx - 1, 10, 30, I_OUT)
    c.vline(cx + 1, 10, 30, I_YELLOW)
    c.ellipse(cx, 30, 4.4, 1.8, T_BROWN)
    # the head: a lantern with something burning in it
    c.rect(cx - 4, 2, cx + 4, 10, I_OUT)
    c.rect(cx - 3, 3, cx + 3, 9, I_YELLOW)
    c.ellipse(cx, 6, 2.2, 2.6, I_WHITE)
    c.hline(cx - 5, cx + 5, 1, I_OUT)
    c.hline(cx - 4, cx + 4, 0, T_BROWN)
    return c


def draw_donald() -> Canvas:
    """Short, round, furious.  The silhouette is the hat and the bill."""
    c = Canvas(32, 32)
    cx = 16

    # webbed feet
    for sign in (1, -1):
        c.ellipse(cx + sign * 4, 30, 3.4, 1.6, I_YELLOW)
    # the coat
    c.rect(cx - 5, 17, cx + 5, 28, I_BLUE)
    c.ellipse(cx, 28, 5.4, 2.6, I_BLUE)
    c.hline(cx - 5, cx + 5, 20, I_YELLOW)          # the trim
    c.hline(cx - 5, cx + 5, 26, I_YELLOW)
    for sign in (1, -1):                            # sleeves
        c.ellipse(cx + sign * 6, 21, 2.0, 3.0, I_BLUE)
        c.ellipse(cx + sign * 6, 24, 1.8, 1.8, I_WHITE)
    c.rect(cx - 3, 16, cx + 3, 19, I_WHITE)         # the collar
    # head
    c.ellipse(cx, 11, 6.0, 5.4, I_WHITE)
    # the bill, which is most of him
    c.ellipse(cx + 1, 14, 5.0, 2.4, I_YELLOW)
    c.ellipse(cx + 1, 15, 4.2, 1.4, I_OUT)
    # sailor cap
    c.ellipse(cx, 6, 6.4, 2.6, I_BLUE)
    c.ellipse(cx, 4, 4.6, 2.6, I_BLUE)
    c.hline(cx - 6, cx + 6, 7, I_WHITE)
    c.rect(cx + 3, 2, cx + 6, 4, I_RED)             # the ribbon
    for side in (-1, 1):                            # eyes
        c.rect(cx + side * 3 - 1, 9, cx + side * 3, 11, I_OUT)
    c.outline(I_OUT)
    return c


def draw_goofy() -> Canvas:
    """Tall, and mostly ears and hat."""
    c = Canvas(32, 32)
    cx = 16

    for sign in (1, -1):                            # the shoes
        c.ellipse(cx + sign * 4, 30, 3.6, 1.8, I_YELLOW)
        c.ellipse(cx + sign * 4, 31, 3.2, 1.0, I_OUT)
    c.rect(cx - 4, 24, cx + 4, 29, I_YELLOW)        # trousers
    c.rect(cx - 5, 17, cx + 5, 25, I_NAVY)          # the vest
    c.ellipse(cx, 25, 5.2, 2.2, I_NAVY)
    c.hline(cx - 5, cx + 5, 17, I_GREEN)
    for sign in (1, -1):                            # sleeves
        c.ellipse(cx + sign * 6, 20, 2.0, 3.2, I_GREEN)
        c.ellipse(cx + sign * 6, 24, 1.8, 1.8, I_WHITE)
    # head, long in the muzzle
    c.ellipse(cx, 11, 5.4, 5.0, T_TAN)
    c.ellipse(cx + 1, 14, 3.6, 2.6, T_TAN)
    c.ellipse(cx + 1, 15, 2.6, 1.4, T_BROWN)
    c.set(cx + 2, 15, I_OUT)
    for side in (-1, 1):                            # the ears, hanging
        c.ellipse(cx + side * 7, 12, 2.0, 4.0, T_BROWN)
    # the hat
    c.ellipse(cx, 6, 6.6, 2.2, I_GREEN)
    c.rect(cx - 3, 1, cx + 3, 6, I_GREEN)
    c.ellipse(cx, 1, 3.2, 1.6, I_GREEN)
    c.hline(cx - 3, cx + 3, 4, I_NAVY)
    for side in (-1, 1):                            # eyes
        c.rect(cx + side * 2 - 1, 9, cx + side * 2, 11, I_OUT)
    c.outline(I_OUT)
    return c


# --- the Guard Armor ------------------------------------------------------
A_OUT, A_STEEL_L, A_STEEL_M, A_STEEL_D = 1, 2, 3, 4
A_RED_L, A_RED_D, A_VIO_L, A_VIO_D = 5, 6, 7, 8
A_BRASS, A_BRASS_D, A_LEATHER, A_GAP = 9, 10, 11, 12
A_EMBLEM, A_SPARK = 13, 14


def draw_guard_armor() -> Canvas:
    """A suit of armour with nobody in it, 64x64.

    Emitted the same way Darkside is -- four 32x32 blocks -- so it needs no
    new path through the sprite code.  The arms are separate actors, because
    in the source they come off.
    """
    c = Canvas(64, 64)
    cx = 32

    # --- the helm, floating clear of the shoulders ---
    c.ellipse(cx, 14, 13.0, 10.0, A_STEEL_M)
    c.ellipse(cx, 12, 11.0, 8.0, A_STEEL_L)
    c.ellipse(cx, 18, 12.0, 5.0, A_STEEL_D)
    c.rect(cx - 9, 15, cx + 9, 21, A_GAP)          # the visor slot
    for side in (-1, 1):                            # and what is looking out
        c.ellipse(cx + side * 5, 18, 2.4, 1.6, A_EMBLEM)
    c.rect(cx - 2, 2, cx + 2, 8, A_RED_L)          # the crest
    c.ellipse(cx, 2, 3.0, 2.0, A_RED_D)
    for side in (-1, 1):                            # horns
        c.ellipse(cx + side * 12, 9, 3.0, 5.0, A_BRASS)
        c.ellipse(cx + side * 12, 8, 2.0, 3.4, A_BRASS_D)

    # --- the torso, hanging under it with a gap between ---
    c.rect(cx - 14, 30, cx + 14, 50, A_RED_L)
    c.ellipse(cx, 30, 14.0, 5.0, A_RED_L)
    c.ellipse(cx, 50, 14.0, 5.0, A_RED_D)
    c.rect(cx - 14, 30, cx - 8, 50, A_RED_D)
    c.rect(cx + 8, 30, cx + 14, 50, A_RED_D)
    for y in range(33, 50, 6):                      # plates
        c.hline(cx - 13, cx + 13, y, A_VIO_D)
        c.hline(cx - 13, cx + 13, y + 1, A_VIO_L)
    # the emblem, in the middle of the chest
    c.ellipse(cx, 40, 6.0, 6.0, A_BRASS)
    c.ellipse(cx, 40, 4.4, 4.4, A_OUT)
    c.ellipse(cx - 2, 39, 2.0, 2.0, A_EMBLEM)
    c.ellipse(cx + 2, 39, 2.0, 2.0, A_EMBLEM)
    c.rect(cx - 3, 40, cx + 3, 43, A_EMBLEM)
    c.set(cx, 45, A_EMBLEM)

    # --- the boots, also floating ---
    for side in (-1, 1):
        bx = cx + side * 11
        c.ellipse(bx, 58, 7.0, 5.0, A_STEEL_M)
        c.ellipse(bx, 56, 5.4, 3.4, A_STEEL_L)
        c.ellipse(bx + side * 2, 61, 6.0, 2.6, A_LEATHER)
        c.hline(bx - 5, bx + 5, 59, A_BRASS)
    c.outline(A_OUT)
    return c


def draw_gauntlet(frame: int) -> Canvas:
    """One of its hands.  Two cels: open, and closed to come down on you."""
    c = Canvas(32, 32)
    cx, cy = 16, 16
    c.ellipse(cx, cy, 10.0, 9.0, A_STEEL_M)
    c.ellipse(cx, cy - 2, 8.0, 6.4, A_STEEL_L)
    c.ellipse(cx, cy + 5, 9.0, 4.0, A_STEEL_D)
    c.rect(cx - 9, cy - 1, cx + 9, cy + 2, A_VIO_D)
    c.hline(cx - 9, cx + 9, cy, A_VIO_L)
    if frame == 0:
        for k, side in enumerate((-6, -2, 2, 6)):   # fingers, spread
            c.rect(cx + side - 1, cy + 6, cx + side + 1, cy + 11, A_STEEL_M)
            c.set(cx + side, cy + 11, A_OUT)
    else:
        c.ellipse(cx, cy + 8, 8.0, 4.0, A_STEEL_M)  # a fist
        c.hline(cx - 7, cx + 7, cy + 9, A_STEEL_D)
    c.ellipse(cx - 4, cy - 4, 2.4, 1.8, A_SPARK)
    c.outline(A_OUT)
    return c


def build_obj_town() -> Canvas:
    """OBJ page two while Traverse Town is up: its cast in place of the
    islanders', who are not going to be in it."""
    page = Canvas(128, 128)
    # rows 0-3: the people already living here
    page.blit(islander(I_YELLOW, I_BLUE, T_BROWN, style="spiky",
                       trim=I_WHITE), 0, 0)             # $00 Cid
    page.blit(islander(T_BROWN, I_GREEN, T_BROWN, style="bob",
                       trim=I_YELLOW), 32, 0)           # $04 a townsman
    page.blit(islander(I_PURPLE, I_RED, I_NAVY, style="flip",
                       trim=I_WHITE), 64, 0)            # $08 a townswoman
    page.blit(draw_lamp(), 96, 0)                       # $0C
    # rows 4-7: the two who fall on him, and a hand
    page.blit(draw_donald(), 0, 32)                     # $40
    page.blit(draw_goofy(), 32, 32)                     # $44
    page.blit(draw_gauntlet(0), 64, 32)                 # $48
    page.blit(draw_gauntlet(1), 96, 32)                 # $4C
    # rows 8-15, cols 0-7: the Guard Armor, as 2x2 blocks of 32x32 at
    # $80 $84 over $C0 $C4 -- the same arrangement Darkside uses.
    page.blit(draw_guard_armor(), 0, 64)
    return page


def build_obj_page2() -> Canvas:
    """The second sprite page: the islanders and what they are after."""
    page = Canvas(128, 128)
    # rows 0-3: four of the five islanders, as 32x32 blocks
    page.blit(islander(I_KAIRI, I_WHITE, I_PURPLE, style="bob",
                       trim=I_PURPLE), 0, 0)                    # $00
    page.blit(islander(I_RIKU, I_YELLOW, I_NAVY, style="long",
                       trim=I_WHITE), 32, 0)                    # $04
    page.blit(islander(I_TIDUS, I_YELLOW, I_BLUE, style="spiky",
                       trim=I_BLUE, prop="sword",
                       prop_col=I_SELPHIE), 64, 0)              # $08
    page.blit(islander(I_SELPHIE, I_YELLOW, I_YELLOW, style="flip",
                       trim=I_WHITE, prop="rope",
                       prop_col=I_WHITE), 96, 0)                # $0C
    # rows 4-7: Wakka and the door, as 32x32 blocks, then the collectables
    # as 16x16 pieces alongside them.  A tile number is (y/8)*16 + x/8, so
    # every blit here has to line up with the TILE_* constants in game.inc.
    page.blit(islander(I_WAKKA, I_BLUE, I_YELLOW, style="up",
                       trim=I_GREEN, prop="ball",
                       prop_col=I_RED), 0, 32)                  # $40
    page.blit(draw_door(), 32, 32)          # $44
    page.blit(draw_log(), 64, 32)           # $48
    page.blit(draw_cloth(), 80, 32)         # $4A
    page.blit(draw_rope(), 96, 32)          # $4C
    page.blit(draw_mushroom(), 112, 32)     # $4E
    # row 6: what day two is after
    page.blit(draw_fish(0), 64, 48)         # $68
    page.blit(draw_fish(1), 80, 48)         # $6A
    page.blit(draw_coconut(), 96, 48)       # $6C
    page.blit(draw_egg(), 112, 48)          # $6E
    # rows 8-11: the bottle, and what is on the cave wall
    page.blit(draw_bottle(), 0, 64)         # $80
    page.blit(draw_faces(), 32, 64)         # $84
    page.blit(draw_scribbles(), 64, 64)     # $88
    page.blit(draw_door_open(), 96, 64)     # $8C
    # rows 12-15: the darkness itself, two cels
    page.blit(draw_dark_pool(0), 0, 96)     # $C0
    page.blit(draw_dark_pool(1), 32, 96)    # $C4
    return page


def build_obj_page() -> Canvas:
    """Assemble the 128x128 sprite page (a 16x16 grid of 8x8 tiles).

    Tiles $00-$3F are left empty: that block is the window Sora's current
    animation frame is streamed into every time it changes.
    """
    page = Canvas(128, 128)
    # rows 0-3: 32x32 blocks.  $00 is the window Sora's current cel streams into.
    page.blit(draw_palm(), 32, 0)           # $04
    page.blit(draw_boulder(), 64, 0)        # $08
    page.blit(draw_shadow_big(), 96, 0)     # $0C
    # rows 4-7: the Dive props
    page.blit(draw_pedestal(), 0, 32)       # $40
    page.blit(draw_sword(), 32, 32)         # $44
    page.blit(draw_shield(), 64, 32)        # $48
    page.blit(draw_staff(), 96, 32)         # $4C
    # rows 8-15, cols 0-7: Darkside, as a 2x2 arrangement of 32x32 blocks
    # with tile bases $80 $84 / $C0 $C4.
    page.blit(draw_darkside(), 0, 64)
    # rows 8-11, cols 8-15: the boss's projectiles
    page.blit(draw_orb(0), 64, 64)          # $88
    page.blit(draw_orb(1), 80, 64)          # $8A
    page.blit(draw_streak(0), 96, 64)       # $8C
    page.blit(draw_streak(1), 112, 64)      # $8E
    # rows 10-11, cols 8-15: the Shadow again, cut from the night palette
    for f in range(4):                      # $A8 $AA $AC $AE
        page.blit(draw_heartless(f, HEART_PAL_NIGHT), 64 + f * 16, 80)
    # row 12, cols 8-15: the Shadow's four cels
    for f in range(4):                      # $C8 $CA $CC $CE
        page.blit(draw_heartless(f), 64 + f * 16, 96)
    # row 14, cols 8-15: the remaining 16x16 pieces
    page.blit(draw_shadow_blob(), 64, 112)  # $E8
    page.blit(draw_slash(0), 80, 112)       # $EA
    page.blit(draw_slash(1), 96, 112)       # $EC
    page.blit(draw_rock(), 112, 112)        # $EE
    return page


# ---------------------------------------------------------------------------
# HUD font (2bpp)
# ---------------------------------------------------------------------------

# 5x7 glyphs.  Every cell is drawn on an opaque dark background so text tiles
# sit seamlessly inside a dialogue window -- BG3 has only one layer, so a
# transparent glyph background would let the ground show through the box.
GLYPHS = {
    "A": (".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"),
    "B": ("####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."),
    "C": (".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."),
    "D": ("####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."),
    "E": ("#####", "#....", "#....", "####.", "#....", "#....", "#####"),
    "F": ("#####", "#....", "#....", "####.", "#....", "#....", "#...."),
    "G": (".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".###."),
    "H": ("#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"),
    "I": ("#####", "..#..", "..#..", "..#..", "..#..", "..#..", "#####"),
    "J": ("....#", "....#", "....#", "....#", "#...#", "#...#", ".###."),
    "K": ("#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"),
    "L": ("#....", "#....", "#....", "#....", "#....", "#....", "#####"),
    "M": ("#...#", "##.##", "#.#.#", "#...#", "#...#", "#...#", "#...#"),
    "N": ("#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#"),
    "O": (".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."),
    "P": ("####.", "#...#", "#...#", "####.", "#....", "#....", "#...."),
    "Q": (".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"),
    "R": ("####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"),
    "S": (".####", "#....", "#....", ".###.", "....#", "....#", "####."),
    "T": ("#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."),
    "U": ("#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."),
    "V": ("#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."),
    "W": ("#...#", "#...#", "#...#", "#...#", "#.#.#", "##.##", "#...#"),
    "X": ("#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"),
    "Y": ("#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."),
    "Z": ("#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"),
}

DIGITS = {
    "0": (".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."),
    "1": ("..#..", ".##..", "..#..", "..#..", "..#..", "..#..", "#####"),
    "2": (".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"),
    "3": ("####.", "....#", "....#", ".###.", "....#", "....#", "####."),
    "4": ("#...#", "#...#", "#...#", "#####", "....#", "....#", "....#"),
    "5": ("#####", "#....", "####.", "....#", "....#", "#...#", ".###."),
    "6": (".###.", "#....", "####.", "#...#", "#...#", "#...#", ".###."),
    "7": ("#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."),
    "8": (".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."),
    "9": (".###.", "#...#", "#...#", ".####", "....#", "....#", ".###."),
}

# Tile numbers, mirrored by the CH_* constants in src/text.inc
PUNCT = {
    37: ("(.)", (".....", ".....", ".....", ".....", ".....", ".##..", ".##..")),
    38: ("(,)", (".....", ".....", ".....", ".....", ".##..", ".##..", ".#...")),
    39: ("(!)", ("..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#..")),
    40: ("(?)", (".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#..")),
    41: ("(')", ("..#..", "..#..", ".....", ".....", ".....", ".....", ".....")),
    42: ("(-)", (".....", ".....", ".....", "#####", ".....", ".....", ".....")),
    43: ("(:)", (".....", "..#..", "..#..", ".....", "..#..", "..#..", ".....")),
    44: ("(/)", ("....#", "....#", "...#.", "..#..", ".#...", "#....", "#....")),
    45: ("cur", ("#....", "##...", "###..", "####.", "###..", "##...", "#....")),
    46: ("adv", (".....", ".....", "#####", ".###.", "..#..", ".....", ".....")),
}

FONT_BG = 3         # opaque box background baked into every glyph cell
FONT_INK = 1
FONT_TRIM = 2


def build_hud_font() -> Canvas:
    """The 2bpp BG3 page: text, gauge pieces, and dialogue window edges."""
    page = Canvas(8 * 16, 8 * 8)           # 16x8 tiles = 128 tiles

    def cell(tile_index: int) -> tuple[int, int]:
        return (tile_index % 16) * 8, (tile_index // 16) * 8

    def fill(tile_index: int, colour: int) -> None:
        tx, ty = cell(tile_index)
        for y in range(8):
            for x in range(8):
                page.set(tx + x, ty + y, colour)

    def glyph(tile_index: int, rows: tuple[str, ...]) -> None:
        fill(tile_index, FONT_BG)
        tx, ty = cell(tile_index)
        for y, row in enumerate(rows):
            for x, ch in enumerate(row):
                if ch == "#":
                    page.set(tx + x + 1, ty + y, FONT_INK)

    fill(0, FONT_BG)                                    # space
    for letter, rows in GLYPHS.items():
        glyph(1 + ord(letter) - ord("A"), rows)
    for d, rows in DIGITS.items():
        glyph(27 + int(d), rows)
    for idx, (_name, rows) in PUNCT.items():
        glyph(idx, rows)

    # --- HP gauge: left cap, full, half, empty, right cap (tiles 48-52) ---
    def bar(tile_index: int, fill_cols: int, cap: str | None = None) -> None:
        fill(tile_index, FONT_BG)
        tx, ty = cell(tile_index)
        for y in range(2, 6):
            for x in range(8):
                page.set(tx + x, ty + y, FONT_TRIM)
        for y in range(3, 5):
            for x in range(fill_cols):
                page.set(tx + x, ty + y, FONT_INK)
        if cap == "L":
            for y in range(1, 7):
                page.set(tx + 1, ty + y, FONT_INK)
        elif cap == "R":
            for y in range(1, 7):
                page.set(tx + 6, ty + y, FONT_INK)

    bar(48, 0, "L")
    bar(49, 8)
    bar(50, 4)
    bar(51, 0)
    bar(52, 0, "R")

    # --- dialogue window edges (tiles 56-64), a 3x3 nine-patch ---
    def window(tile_index: int, left: bool, right: bool,
               top: bool, bottom: bool) -> None:
        fill(tile_index, FONT_BG)
        tx, ty = cell(tile_index)
        if top:
            for x in range(8):
                page.set(tx + x, ty + 1, FONT_TRIM)
        if bottom:
            for x in range(8):
                page.set(tx + x, ty + 6, FONT_TRIM)
        if left:
            for y in range(8):
                page.set(tx + 1, ty + y, FONT_TRIM)
        if right:
            for y in range(8):
                page.set(tx + 6, ty + y, FONT_TRIM)

    window(56, True, False, True, False)    # top-left
    window(57, False, False, True, False)   # top
    window(58, False, True, True, False)    # top-right
    window(59, True, False, False, False)   # left
    window(60, False, False, False, False)  # interior (plain fill)
    window(61, False, True, False, False)   # right
    window(62, True, False, False, True)    # bottom-left
    window(63, False, False, False, True)   # bottom
    window(64, False, True, False, True)    # bottom-right

    # Tile 127 is left untouched -- an all-zero, fully transparent cell.
    # Every glyph carries an opaque background, so something has to mean
    # "nothing here" for the rows outside the dialogue box.
    return page


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------

def load_grid(name: str = "island.txt", subdir: str = "") -> Grid:
    """Read a map.  Its dimensions come from the file; every row must agree.

    Returns a Grid, which is list-like enough for the original callers.
    """
    path = ROOT / "assets" / subdir / name if subdir else ROOT / "assets" / name
    text = path.read_text().splitlines()
    # A comment is "#" alone or "# ...".  Terrain codes include "#", so a
    # map row that starts with cliff rock must not be mistaken for one.
    rows = [ln for ln in text
            if ln and not (ln[0] == "#" and ln[1:2] in ("", " "))]
    if not rows:
        raise SystemExit(f"{name}: no map rows")
    width = len(rows[0])
    for n, row in enumerate(rows):
        if len(row) != width:
            raise SystemExit(f"{name} row {n}: {len(row)} columns, but row 0 "
                             f"has {width} -- every row must be the same width")
        for ch in row:
            if ch not in TERRAIN:
                raise SystemExit(f"{name} row {n}: unknown terrain '{ch}'")
    return Grid(rows, name)


# ---------------------------------------------------------------------------
# The cast
#
# A scene is a map plus the things standing on it.  On the SNES both halves were
# hand-written: a table of (type, i, j) triples in assembly, and the map, with
# nothing checking that they agreed.  They did agree -- every T tile carried
# exactly one ACT_PALM and so on for R, Y, r and l -- but only because somebody
# kept them in step by hand, and a T with no palm on it is an invisible wall.
#
# So the correspondence is a rule here rather than a coincidence.  Prop actors
# are DERIVED from the map: the tile says what stands on it, and the cast file
# never mentions a palm.  Adding a tree to a map is adding one character.
# ---------------------------------------------------------------------------

# tile code -> the actor that must be standing on it, and nothing else may be.
PROP_ACTOR = {
    "T": "Palm",
    "Y": "PalmC",       # a palm still carrying its coconuts
    "R": "RockBig",
    "r": "Rock",
    "l": "Lamp",        # a street lamp, so somebody can walk behind it
}

# ...except where a scene says otherwise.  The night runs on the island's map
# but every coconut has been picked by then -- night.s spawns a plain ACT_PALM
# at both trees the days give an ACT_PALMC, and calls them "picked clean".  That
# is a property of the scene, not of the tile, so it lives here.
NIGHT_PROPS = {"Y": "Palm"}


def _act_types() -> dict[str, int]:
    """Read the actor type numbers out of the DS header rather than repeating them.

    There were two copies of this enumeration already (game.inc for the SNES and
    actor.h for the DS, kept equal by hand and pinned by static_assert).  A third
    in the asset pipeline would be the one nothing checks, so parse the header:
    if it will not parse, that is a hard error and not a silent fallback.
    """
    path = ROOT / "platform" / "ds" / "include" / "actor.h"
    text = path.read_text()
    body = text.split("enum class ActType", 1)
    if len(body) != 2:
        raise SystemExit(f"{path}: no 'enum class ActType' to read type numbers from")
    body = body[1].split("};", 1)[0]
    out: dict[str, int] = {}
    for name, num in re.findall(r"^\s*(\w+)\s*=\s*(\d+)\s*,", body, re.M):
        out[name] = int(num)
    if "Sora" not in out or "Lamp" not in out:
        raise SystemExit(f"{path}: parsed {len(out)} actor types but not the ones "
                         f"the cast files use -- has the enum changed shape?")
    return out


ACT = _act_types()

# Sections that mean something specific.  Any OTHER section name is a table of
# actors the scene spawns at a moment of its own choosing -- the second day's
# food, the two who fall out of the sky in the Third District -- because those
# moments are scene logic and the pipeline has no business enumerating them.
CAST_END = 0xFF                         # terminates every emitted table


class Cast:
    """One scene's cast: what is placed, where, and when.

    `base` is placed on entry and again after a death.  `spots` is where the
    Heartless come up.  `doors` is a door tile and the tile Sora is stood on
    beside it.  Everything else is a named table, kept separate for the reason
    island.s kept two of them: merged, they would spawn the second day's
    mushrooms on the first.
    """

    __slots__ = ("base", "tables", "spots", "doors", "name")

    def __init__(self, name: str):
        self.name = name
        self.base: list[tuple[str, int, int, int]] = []
        self.tables: dict[str, list[tuple[str, int, int, int]]] = {}
        self.spots: list[tuple[int, int]] = []
        self.doors: list[tuple[int, int, int, int]] = []

    @property
    def actors(self):
        """Every authored actor row, whichever moment it is placed at."""
        return self.base + [r for t in self.tables.values() for r in t]

    @property
    def peak(self):
        """The most actors that can be resident at once, counting generously.

        Every table summed rather than the largest taken: some are alternatives
        (the island's two days) and some are additive (Donald and Goofy join a
        district that is already populated), and the pipeline cannot tell which
        without knowing the scene's logic.  Over-counting a pool budget is the
        safe direction to be wrong in.
        """
        return len(self.base) + sum(len(t) for t in self.tables.values())


def load_cast(stem: str) -> Cast:
    """Read assets/ds/<stem>_cast.txt.  Missing file means an empty cast."""
    path = ROOT / "assets" / "ds" / f"{stem}_cast.txt"
    cast = Cast(stem)
    if not path.exists():
        return cast
    section = None
    for n, raw in enumerate(path.read_text().splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        where = f"{path.name}:{n}"
        if line[0] == "[":
            section = line.strip("[]").strip()
            if not section.isidentifier():
                raise SystemExit(f"{where}: '{section}' is not usable as a "
                                 f"table name")
            if section not in ("base", "spots", "doors"):
                cast.tables.setdefault(section, [])
            continue
        if section is None:
            raise SystemExit(f"{where}: a row before any [section] header")
        f = line.split()
        if section == "spots":
            if len(f) != 2:
                raise SystemExit(f"{where}: a spot is 'i j', got {len(f)} fields")
            cast.spots.append((int(f[0]), int(f[1])))
        elif section == "doors":
            if len(f) != 4:
                raise SystemExit(f"{where}: a door is 'i j land_i land_j', "
                                 f"got {len(f)} fields")
            cast.doors.append(tuple(int(v) for v in f))   # type: ignore[arg-type]
        else:
            if len(f) not in (3, 4):
                raise SystemExit(f"{where}: an actor is 'Type i j [variant]', "
                                 f"got {len(f)} fields")
            if f[0] not in ACT:
                raise SystemExit(f"{where}: '{f[0]}' is not an ActType")
            if f[0] in PROP_ACTOR.values():
                raise SystemExit(
                    f"{where}: '{f[0]}' is derived from the map, not authored -- "
                    f"put the tile down instead and it will appear")
            row = (f[0], int(f[1]), int(f[2]), int(f[3]) if len(f) == 4 else 0)
            if not 0 <= row[3] <= 255:
                raise SystemExit(f"{where}: variant {row[3]} does not fit a byte")
            (cast.base if section == "base" else cast.tables[section]).append(row)
    return cast


def derive_props(grid, override=None) -> list[tuple[str, int, int, int]]:
    """Every prop the map asks for, north to south then west to east.

    The order is the map's, so a scene's prop slots are stable under an edit
    somewhere else on the map -- which is what keeps a trace diff readable.
    """
    rule = dict(PROP_ACTOR, **(override or {}))
    return [(rule[grid[j][i]], i, j, 0)
            for j in range(grid.h) for i in range(grid.w)
            if grid[j][i] in rule]


def cast_bytes(rows) -> bytes:
    """(type, i, j, variant) rows, terminated -- the walk the DS loader does.

    Four bytes rather than the SNES's three.  The fourth is a variant, and it is
    the reason this format is not just the old one: dialogue dispatched on actor
    TYPE, so six townspeople all said the same sentence, and six identical
    strangers read worse than two.  Nothing consumes it until M5; it is here
    now because adding a field later means re-authoring every file.
    """
    out = bytearray()
    for name, i, j, var in rows:
        out += bytes((ACT[name], i, j, var))
    out.append(CAST_END)
    return bytes(out)


# Marker colours for the preview, appended above the sixteen the ground uses.
# The preview is how placement gets reviewed, and a cast that cannot be seen is
# a cast nobody checks.
CAST_MARKERS = [
    (0, 0, 0),              # 16: outline, so a marker reads on any ground
    (255, 208, 64),         # 17: somebody who can be talked to
    (96, 232, 255),         # 18: something that can be taken
    (120, 200, 120),        # 19: a derived prop, or scenery placed by hand
    (255, 96, 96),          # 20: where a Heartless comes up
    (255, 255, 255),        # 21: a door, and the tile it lands on
    (255, 128, 255),        # 22: the wall of the Secret Place
    (255, 144, 32),         # 23: Sora
    (176, 0, 96),           # 24: a boss
]
MARK_OUTLINE, MARK_PERSON, MARK_ITEM, MARK_PROP = 16, 17, 18, 19
MARK_SPOT, MARK_DOOR, MARK_WALL, MARK_SORA, MARK_BOSS = 20, 21, 22, 23, 24


def _marker_of(name: str) -> int:
    if name == "Sora":
        return MARK_SORA
    if name in ("Darkside", "Armor", "Gauntlet"):
        return MARK_BOSS
    # A pedestal is scenery, not somebody -- and gold-on-gold is invisible on a
    # Station of Awakening, which is the only place either of these appears.
    if name in PROP_ACTOR.values() or name == "Pedestal":
        return MARK_PROP
    if is_pickup(name) or name in ("Fish", "Sword", "Shield", "Staff"):
        return MARK_ITEM
    if name in ("Door", "Faces", "Scribble", "DoorOpen"):
        return MARK_WALL
    if name == "Shadow":
        return MARK_SPOT
    return MARK_PERSON


def is_pickup(name: str) -> bool:
    """Log..Bottle, the range whose order also indexes the item tally."""
    return ACT["Log"] <= ACT.get(name, 0) <= ACT["Bottle"]


def draw_cast(world, rows, marker_of=_marker_of) -> None:
    """Stamp a marker per cast entry onto a copy of the painted world.

    A tile is 16 px, so a marker sits in the middle of one and cannot be
    mistaken for terrain: a filled diamond in a black surround.
    """
    for row in rows:
        if len(row) == 2:                       # a Heartless spot: no type
            (i, j), mark = row, MARK_SPOT
        elif len(row) == 4 and isinstance(row[0], str):
            _, i, j, _ = row
            mark = marker_of(row[0])
        else:                                   # a door and the tile it lands on
            i, j, mark = row[0], row[1], MARK_DOOR
        cx, cy = i * TILE + 8, j * TILE + 8
        for dy in range(-5, 6):
            for dx in range(-5, 6):
                d = abs(dx) + abs(dy)
                if d > 5:
                    continue
                x, y = cx + dx, cy + dy
                if 0 <= x < world.w and 0 <= y < world.h:
                    world.set(x, y, mark if d <= 3 else MARK_OUTLINE)


def grid_from_coll(coll: bytes, w: int, h: int, name: str = "") -> Grid:
    """A Grid standing in for a map that was generated rather than authored.

    The Stations of Awakening have no text map: build_dive_platform() paints the
    glass and derives the standable set from a circle.  Everything downstream --
    the cast checker, the prop derivation, the reachability walk -- wants a Grid,
    and there is no reason for any of them to know the difference, so make one.
    'G' is standable glass and '*' is the void, and neither is ever drawn.
    """
    return Grid(["".join("G" if coll[j * w + i] else "*" for i in range(w))
                 for j in range(h)], name)


class DSScene:
    """A DS scene: a map, a palette, a cast, and possibly somebody else's ground.

    Three things stop this from being just a filename:

    - Two scenes can share one map.  The night runs on the island's tiles,
      tilemap, collision and height -- what makes it night is one palette upload,
      exactly as on the SNES -- so it must not re-emit 28 KB of identical ground,
      and it must not be given the island's cast either.
    - A map need not be authored.  A station is generated from a circle, so the
      scene carries a builder instead of a filename.
    - A map need not live in assets/ds/.  The fragment is deliberately
      unexpanded and reads the SNES one.
    """

    __slots__ = ("name", "map", "subdir", "palette", "ground", "props",
                 "note", "build")

    def __init__(self, name, map_, subdir, palette,
                 ground=None, props=None, note="", build=None):
        self.name = name
        self.map = map_
        self.subdir = subdir
        self.palette = palette
        self.ground = ground        # emit our own if None, else share that scene's
        self.props = props          # PROP_ACTOR override
        self.note = note
        self.build = build          # () -> (world, coll, height, grid)

    def painted(self):
        """The scene's ground: painted canvas, collision, height, and a Grid."""
        if self.build is not None:
            return self.build()
        grid = load_grid(self.map, subdir=self.subdir)
        world, coll, hmap = build_world(grid)
        return world, coll, hmap, grid

    def grid(self):
        return self.painted()[3]


def _station(n: int):
    """The nth Station of Awakening, at the radius the DS screen can show."""
    def build():
        world, coll = build_dive_platform(station=n, radius=DS_DIVE_R)
        grid = grid_from_coll(coll, MAP_W, MAP_H, f"station{n}")
        return world, coll, bytes(len(coll)), grid       # flat glass: height 0
    return build


def ds_scenes():
    """Every DS scene, in build order.  check_map.py walks the same list."""
    return (
        # The three Stations of Awakening.  Not expanded -- a station is a disc
        # in a void, deliberately smaller than the screen -- but REDRAWN, because
        # the SNES's 220 px disc does not fit the DS's 192 lines.  See
        # docs/behaviour/divergences/004-ds-station-radius.md.
        DSScene("station1", None, "", BG_DIVE, build=_station(1),
                note="the dais and the three dream weapons"),
        DSScene("station2", None, "", BG_DIVE, build=_station(2),
                note="he lands alone, and three of them are already waiting"),
        DSScene("station3", None, "", BG_DIVE, build=_station(3),
                note="Darkside"),
        DSScene("island", "island.txt", "ds", BG_GROUND),
        # The two days' island, at night, with the coconuts gone.
        DSScene("night", "island.txt", "ds", BG_NIGHT,
                ground="island", props=NIGHT_PROPS,
                note="the island's ground, one palette later"),
        # Deliberately NOT expanded: the last scrap of ground after the island
        # comes apart is sized so Darkside can stand on it and Sora cannot
        # retreat.  So it reads the SNES map, from assets/ rather than assets/ds/.
        DSScene("fragment", "fragment.txt", "", BG_NIGHT,
                note="unexpanded on purpose; see docs/WORLD_SIZES.md"),
        DSScene("town1", "town1.txt", "ds", BG_TOWN),
        DSScene("town2", "town2.txt", "ds", BG_TOWN),
        DSScene("town3", "town3.txt", "ds", BG_TOWN),
    )


def build_ds_cast(stem: str, grid, world, palette, props=None) -> tuple[Cast, int]:
    """Emit one scene's cast tables, and a preview with every entry marked."""
    out = GEN / "ds"
    out.mkdir(parents=True, exist_ok=True)
    cast = load_cast(stem)
    prop_rows = derive_props(grid, props)

    write_bin(out / f"{stem}cast.bin", cast_bytes(prop_rows + cast.base))
    for name, rows in cast.tables.items():
        write_bin(out / f"{stem}{name}.bin", cast_bytes(rows))
    if cast.spots:
        write_bin(out / f"{stem}spots.bin",
                  bytes(v for s in cast.spots for v in s) + bytes((CAST_END,)))
    if cast.doors:
        write_bin(out / f"{stem}doors.bin",
                  bytes(v for d in cast.doors for v in d) + bytes((CAST_END,)))

    marked = Canvas(world.w, world.h)
    marked.px = [row[:] for row in world.px]
    draw_cast(marked, prop_rows + cast.actors + cast.spots
              + [(d[0], d[1]) for d in cast.doors]
              + [(d[2], d[3]) for d in cast.doors])
    write_png(marked, list(palette) + CAST_MARKERS,
              SRC / f"ds_{stem}_cast.png")

    peak = len(prop_rows) + cast.peak
    extra = ", ".join(f"{len(v)} {k}" for k, v in cast.tables.items())
    print(f"     cast: {len(prop_rows)} props from the map + {len(cast.base)} "
          f"placed{f' + {extra}' if extra else ''}, peak {peak}"
          f"{f', {len(cast.spots)} spots' if cast.spots else ''}"
          f"{f', {len(cast.doors)} doors' if cast.doors else ''}")
    return cast, peak


def verify_ds_roundtrip(scene, world) -> None:
    """Decode the emitted bytes back and compare against the painted world.

    Neither the character encoding nor the map packing can be checked against
    hardware from here, and both fail by producing a plausible picture rather
    than an error -- a planar tile read as linear is a recognisable pattern in
    the wrong shape.  A round trip is the strongest check available without a
    DS: if the bytes decode to the pixels that went in, the only way they are
    still wrong is if BOTH halves are wrong in exactly compensating ways.
    """
    out = GEN / "ds"
    chars = (out / f"{scene.name}chr.bin").read_bytes()
    tmap = (out / f"{scene.name}map.bin").read_bytes()
    cw, ch = world.w // 8, world.h // 8
    for ty in range(ch):
        for tx in range(cw):
            k = (ty * cw + tx) * 2
            e = tmap[k] | (tmap[k + 1] << 8)
            tile = decode_tile_4bpp_ds(chars, (e & 0x03FF) * 32)
            if e & (1 << 10):
                tile = [list(reversed(r)) for r in tile]
            if e & (1 << 11):
                tile = list(reversed(tile))
            for y in range(8):
                for x in range(8):
                    if tile[y][x] != world.px[ty * 8 + y][tx * 8 + x]:
                        raise SystemExit(
                            f"ds/{scene.name}: character ({tx},{ty}) does not "
                            f"decode back to the pixels that went in")


def build_ds_sprites(sora, obj, obj2, objtown, font) -> list[tuple[str, str]]:
    """The sprite pages and the font, re-encoded for the DS.

    CEL ORDERING IS THE TRAP.  Under the DS's 1D sprite mapping a 32x32 sprite is
    sixteen CONSECUTIVE characters.  The SNES object pages are 16-character-wide
    grids in which a 32x32 object occupies a 4x4 block, so serialising one
    row-major gives four characters of one object followed by four of the next
    and every sprite comes out as a stripe of four different things.

    Sora's sheet needs no reordering -- the SNES already stored it cel-contiguous
    because its streaming DMA uploaded a frame as four 128-byte rows -- but the
    object pages do, and reordering them RENUMBERS every object.  The translation
    is emitted as dsTileFor() rather than by renumbering actor.h's forty-one
    tileFor() values, because those are cited against the assembly and should stay
    citable.
    """
    out = GEN / "ds"
    out.mkdir(parents=True, exist_ok=True)
    made: list[tuple[str, int, str]] = []

    def page(name: str, data: bytes, shape: str) -> int:
        write_bin(out / f"{name}.bin", data)
        made.append((name, len(data), shape))
        return len(data)

    sora_n = page("sorachr", encode_cels_ds(sora, 32, 6, 5),
                  "30 cels of 32x32, six frames by five drawn facings")
    sizes = {"sorachr": sora_n}
    for name, sheet in (("objchr", obj), ("obj2chr", obj2),
                        ("objtownchr", objtown)):
        sizes[name] = page(name, encode_cels_ds(sheet, 32, 4, 4),
                           "16 cels of 32x32, re-serialised cel-contiguous")

    # The font is addressed one character at a time, so row-major is right and
    # cel ordering would be meaningless.  It goes from 2bpp to 4bpp because a DS
    # text background has no 2bpp mode -- twice the bytes, same picture.
    page("hudchr", encode_page_ds(font),
         "128 characters, 4bpp because the DS has no 2bpp BG")

    check_ds_obj_reach(sizes)
    return made


def check_ds_obj_reach(sizes: dict[str, int]) -> None:
    """Does the resident set of object pages fit inside the ten-bit tile number?

    THE MARGIN IS 1024 BYTES AND NOTHING ELSE RECORDS IT.  An OAM tile number is
    ten bits, and at the default 1D boundary of 32 that reaches exactly the first
    32 KiB of object VRAM -- however much of it the machine has, which is 256 KiB.
    Past that the number aliases back to the start of the page, so the symptom is
    the wrong sprite rather than a fault.

    The resident set is the SNES's: Sora's sheet and the first object page are
    always up, and the second page is shared -- LoadScene uploads objChr and
    obj2Chr in every scene (main.s:268-269) and the town overwrites the second
    with objTownChr (main.s:540).  So the town's page is not an addition, and the
    three-page total is what has to fit.

    A fourth resident page would not, and the way out is boundary 64 -- which
    HALVES every cel's tile number.  That is why OBJ_BOUNDARY is emitted as a
    constant and dsTileFor() is derived from it rather than multiplying by a
    literal 16.
    """
    second = max(sizes["obj2chr"], sizes["objtownchr"])
    total = sizes["sorachr"] + sizes["objchr"] + second
    if total > DS_OBJ_REACH:
        raise SystemExit(
            f"the resident object pages are {total} bytes, past the "
            f"{DS_OBJ_REACH} a ten-bit tile number reaches at boundary "
            f"{DS_OBJ_BOUNDARY}. Raise the boundary to "
            f"{DS_OBJ_BOUNDARY * 2} -- and note that halves every cel's tile "
            f"number, so dsTileFor() changes with it")
    print(f"ds/obj:   {total} of {DS_OBJ_REACH} addressable bytes at boundary "
          f"{DS_OBJ_BOUNDARY} ({DS_OBJ_REACH - total} spare), "
          f"sora {sizes['sorachr']} + obj {sizes['objchr']} + "
          f"max(obj2 {sizes['obj2chr']}, town {sizes['objtownchr']})")


def build_ds_palettes() -> list[tuple[str, int]]:
    """Every palette the DS needs, in the format it already had.

    Fifteen-bit BGR little-endian is byte-identical between the two machines, so
    these are the SNES bytes unchanged.  Which slot each one occupies in palette
    RAM is NOT decided here -- that is the VRAM map's business (§M4), and guessing
    it now would be a number two files disagree about later.
    """
    out = GEN / "ds"
    out.mkdir(parents=True, exist_ok=True)
    pals = (("bgpal", BG_GROUND, 16), ("nightpal", BG_NIGHT, 16),
            ("townpal", BG_TOWN, 16), ("divepal", BG_DIVE, 16),
            ("objpal", OBJ_SCENE, 16), ("nightobjpal", OBJ_SCENE_NIGHT, 16),
            ("townobjpal", OBJ_TOWN, 16), ("islepal", OBJ_ISLE, 16),
            ("sorapal", OBJ_SORA, 16), ("shadowpal", OBJ_SHADOW, 16),
            ("divobjpal", OBJ_DIVE, 16), ("armorpal", OBJ_ARMOR, 16),
            ("fxpal", OBJ_FX, 16), ("heartpal", OBJ_HEART, 16),
            ("nightscenepal", OBJ_NIGHT, 16),
            ("hudpal", HUD_PAL, 16))
    made = []
    for name, pal, count in pals:
        write_bin(out / f"{name}.bin", palette_bytes(pal, count))
        made.append((name, count))
    return made


def emit_ds_asset_header(scenes, sprites, palettes) -> None:
    """One header naming every DS asset, so nothing is addressed by filename.

    The brief asks for "a .h/.bin pair per scene, or a single archive -- your
    choice, but document it and keep it stable".  This is the .h half, one file
    for all of them: the binaries stay separate so a scene loads only its own,
    and the header is what says which those are and what shape they have.
    """
    path = ROOT / "platform" / "ds" / "include" / "gen" / "assets.h"
    path.parent.mkdir(parents=True, exist_ok=True)
    L: list[str] = []
    w = L.append
    w("#pragma once")
    w("// GENERATED by tools/build_assets.py -- do not edit.")
    w("//")
    w("// Every DS asset the pipeline emits, with its shape.  The bytes live in")
    w("// assets/gen/ds/ as separate files so a scene loads only its own; this")
    w("// header is the record of what those files are.")
    w("//")
    w("// FORMATS (tools/ds_encode.py has the reasoning):")
    w("//   chr     4bpp LINEAR, 32 bytes a character, left pixel in the low")
    w("//           nibble.  NOT the SNES's planar layout.")
    w("//   map     16-bit entries, ROW-MAJOR and not in hardware block order --")
    w("//           every scene but the stations and the fragment is wider than")
    w("//           the 64 characters one background holds, so the renderer")
    w("//           streams a window through bgEntryIndex() below.")
    w("//           Entry: bits 0-9 character, 10 H flip, 11 V flip, 12-15 palette.")
    w("//   pal     15-bit BGR little-endian -- byte-identical to the SNES word.")
    w("//   coll    one byte a tile, non-zero means standable.")
    w("//   height  one byte a tile, in eight-pixel steps.")
    w("//")
    w("// Every one of those was checked against GBATEK v3.06 and fullsnes, and")
    w("// cross-checked against libnds, by three independent readings; see")
    w("// docs/DS_FORMATS.md for what they said and what they caught.")
    w("")
    w("#include <cstdint>")
    w("")
    w("namespace kh {")
    w("")
    w("// Which ENTRY of a DS text background's map holds character (x, y).")
    w("// Entries, not bytes -- double it for a byte offset.  A background is")
    w("// built from 32x32-character BLOCKS, not rows; a flat 64-wide row-major")
    w("// array comes out horizontally halved and vertically doubled, which reads")
    w("// as a corrupt tileset rather than a layout bug.  Defined once, here, for")
    w("// the streamer and for anything that uploads a map directly.")
    w("//")
    w("// 64x32 and 32x64 both put their second block at entry 1024 and differ")
    w("// only in whether it is the right half or the bottom half, which is why")
    w("// the shape is a parameter.  Out-of-range coordinates WRAP, as the")
    w("// hardware does -- without that, y = 64 in a 64x64 background computes")
    w("// block 4 and writes 2 KB past the end of the map.")
    w("constexpr int bgEntryIndex(int x, int y, int widthChars, int heightChars) {")
    w("    x %= widthChars;")
    w("    y %= heightChars;")
    w("    return ((x >= 32 ? 1 : 0)")
    w("            + (y >= 32 ? (widthChars > 32 ? 2 : 1) : 0)) * 1024")
    w("           + (y % 32) * 32 + (x % 32);")
    w("}")
    w("")
    w("constexpr uint16_t MAP_TILE_MASK = 0x03FF;")
    w("constexpr uint16_t MAP_FLIP_H = 1 << 10;")
    w("constexpr uint16_t MAP_FLIP_V = 1 << 11;")
    w("constexpr int MAP_PAL_SHIFT = 12;")
    w("constexpr int BG_MAX_CHARS = %d;" % DS_BG_MAX_CHARS)
    w("constexpr int BG_BLOCK_ENTRIES = 1024;      // 32x32 characters, 2 KiB")
    w("")
    w("// Sprites.  DISPCNT bit 4 selects 1D mapping and bits 20-21 the boundary;")
    w("// a tile number is a byte offset divided by the boundary, and the number")
    w("// is ten bits, so at boundary 32 it reaches only the first 32 KiB of")
    w("// object VRAM however much of it the machine has.")
    w("//")
    w("// THE BYTES NEVER CHANGE WITH THE BOUNDARY AND THE NUMBERS ALWAYS DO.")
    w("// Whatever sets DISPCNT must set it to OBJ_BOUNDARY, because dsTileFor()")
    w("// below is derived from it: at 64 every cel's tile number halves.")
    w("constexpr int OBJ_BOUNDARY = %d;" % DS_OBJ_BOUNDARY)
    w("constexpr int OBJ_REACH = %d;" % DS_OBJ_REACH)
    w("constexpr int OBJ_CEL_BYTES = 512;          // 32x32 at 4bpp")
    w("constexpr int OBJ_CEL_TILES = OBJ_CEL_BYTES / OBJ_BOUNDARY;")
    w("")
    w("// A 32x32 object is sixteen CONSECUTIVE characters under 1D mapping, so")
    w("// the object pages are re-serialised cel-contiguous and every object's")
    w("// index moves.  actor.h's tileFor() still returns the SNES page offsets --")
    w("// they are cited against the assembly and should stay citable -- so this")
    w("// is the translation.  A SNES page is a 16-character-wide grid in which a")
    w("// 32x32 object occupies a 4x4 block.")
    w("constexpr int dsTileFor(int snesTile) {")
    w("    return (((snesTile / 64) * 4) + ((snesTile % 64) / 4)) * OBJ_CEL_TILES;")
    w("}")
    w("")
    w("struct SceneAsset {")
    w("    const char* name;")
    w("    uint16_t tilesW;")
    w("    uint16_t tilesH;")
    w("    uint16_t chars;         // unique characters, 0 if the ground is shared")
    w("    const char* groundFrom; // nullptr unless it borrows another scene's")
    w("    bool streams;           // too big for one background")
    w("    int8_t bgSize;          // BGxCNT size code, -1 if it streams")
    w("};")
    w("")
    w("constexpr SceneAsset SCENE_ASSETS[] = {")
    for name, gw, gh, chars, shared, streams, bgsize in scenes:
        share = f'"{shared}"' if shared else "nullptr"
        w(f'    {{"{name}", {gw}, {gh}, {chars}, {share}, '
          f'{"true" if streams else "false"}, {bgsize}}},')
    w("};")
    w("")
    w("struct PaletteAsset { const char* name; uint16_t entries; };")
    w("constexpr PaletteAsset PALETTE_ASSETS[] = {")
    for name, count in palettes:
        w(f'    {{"{name}", {count}}},')
    w("};")
    w("")
    w("struct SpriteAsset { const char* name; uint32_t bytes; const char* shape; };")
    w("constexpr SpriteAsset SPRITE_ASSETS[] = {")
    for name, nbytes, shape in sprites:
        w(f'    {{"{name}", {nbytes}, "{shape}"}},')
    w("};")
    w("")
    sizes = {name: n for name, n, _ in sprites}
    resident = (sizes["sorachr"] + sizes["objchr"]
                + max(sizes["obj2chr"], sizes["objtownchr"]))
    w("// The resident set, and the margin.  Sora's sheet and the first object")
    w("// page are always up; the second page is shared, because the town")
    w("// overwrites it (main.s:540) rather than adding to it.  A FOURTH resident")
    w("// page does not fit, and the way out is boundary 64 -- which renumbers")
    w("// every cel.  tools/build_assets.py checks this at build time too; the")
    w("// static_assert is here so a hand-edited OBJ_BOUNDARY cannot get past it.")
    w("constexpr uint32_t OBJ_RESIDENT_BYTES = %d;" % resident)
    w("static_assert(OBJ_RESIDENT_BYTES <= OBJ_REACH,")
    w('              "the resident object pages do not fit the ten-bit tile '
      'number; raise OBJ_BOUNDARY and regenerate");')
    w("")
    w("}  // namespace kh")
    text = "\n".join(L) + "\n"
    if not path.exists() or path.read_text() != text:
        path.write_text(text)


def build_ds_scene(scene: DSScene):
    """Emit one DS scene: characters, a row-major tilemap, collision, height, cast.

    Not the full M2 backend -- no DS palette encoding and no header generation
    yet.  This is what authoring a map needs in order to be validated at all,
    and it lands the row-major tilemap writer that M2 wants anyway.

    A scene with `ground` set emits no ground of its own: it is another scene's
    map under a different palette, so the characters, tilemap, collision and
    height are byte-identical and duplicating them would be 28 KB of lie.
    """
    out = GEN / "ds"
    out.mkdir(parents=True, exist_ok=True)
    world, coll, hmap, grid = scene.painted()
    if scene.ground is None:
        # dedupe_tilemap_ds, not dedupe_tilemap: the DS's characters are LINEAR
        # 4bpp where the SNES's are planar, and its map entries carry the flip
        # bits at 10 and 11 where the SNES has them at 14 and 15.  Reusing the
        # SNES encoder produces a recognisable but wrong picture, which is worse
        # than noise.  See tools/ds_encode.py.
        chars, tilemap, n = dedupe_tilemap_ds(world)
        write_bin(out / f"{scene.name}chr.bin", chars)
        write_bin(out / f"{scene.name}map.bin", tilemap)
        write_bin(out / f"{scene.name}coll.bin", coll)
        write_bin(out / f"{scene.name}height.bin", hmap)
        # Fifteen-bit BGR, little-endian -- byte-identical to the SNES word, so
        # the encoder is reused verbatim.  This is the one format the two
        # machines share exactly.
        write_bin(out / f"{scene.name}pal.bin", palette_bytes(scene.palette))
        write_png(world, scene.palette, SRC / f"ds_{scene.name}_preview.png")
        # Which background shape it needs, if any one of them will do.  A scene
        # that fits can be uploaded whole and scrolled by the hardware; one that
        # does not has to be streamed, and the shape of the window it streams
        # into is the VRAM map's business (§M4), so it is recorded as -1 rather
        # than guessed at here.
        cw, ch = world.w // 8, world.h // 8
        fit = ds_bg_fit(cw, ch)
        streams = "streams" if fit is None else \
            f"fits one BG, BGxCNT size {fit[2]} ({fit[0]}x{fit[1]})"
        print(f"ds/{scene.name}: {grid.w}x{grid.h} tiles, {world.w}x{world.h} px, "
              f"{cw}x{ch} chars, {n} unique, "
              f"{sum(coll)} walkable, {streams}")
        verify_ds_roundtrip(scene, world)
        shape = (scene.name, grid.w, grid.h, n, None, fit is None,
                 -1 if fit is None else fit[2])
    else:
        # Still worth a preview: the palette is the whole difference and the
        # only way to see whether it reads as the same place after dark.
        write_png(world, scene.palette, SRC / f"ds_{scene.name}_preview.png")
        # ...and the palette itself, which IS the difference, so it is the one
        # thing a ground-sharing scene still emits.
        write_bin(out / f"{scene.name}pal.bin", palette_bytes(scene.palette))
        print(f"ds/{scene.name}: {grid.w}x{grid.h} tiles, ground shared with "
              f"{scene.ground} -- {scene.note}")
        fit = ds_bg_fit(world.w // 8, world.h // 8)
        shape = (scene.name, grid.w, grid.h, 0, scene.ground, fit is None,
                 -1 if fit is None else fit[2])
    build_ds_cast(scene.name, grid, world, scene.palette, scene.props)
    return shape


def main(argv=None) -> int:
    """--target snes | ds | both.

    The default is BOTH, which is not what the brief guessed, and the reason is
    that the DS output already has consumers: the host tests read the emitted
    collision maps and cast tables, so a default that skipped them would make
    `make -f Makefile.host run` fail on a clean tree.

    `--target snes` is a true subset: it writes the oracle's artefacts and
    nothing else.  `--target ds` IS NOT the mirror of it -- it writes the DS
    assets, and regenerates the SNES ones as a by-product, because the DS pass
    consumes canvases the SNES pass paints and separating them would mean
    threading a suppression flag through thirty-five write sites for no gain.
    The by-product is byte-identical, which Gate 0 checks, so the only cost is a
    few hundred milliseconds.  Said plainly here because a flag that quietly does
    more than its name is worse than one that admits it.
    """
    argv = sys.argv[1:] if argv is None else argv
    target = "both"
    for i, a in enumerate(argv):
        if a == "--target" and i + 1 < len(argv):
            target = argv[i + 1]
    if target not in ("snes", "ds", "both"):
        raise SystemExit(f"--target must be snes, ds or both, not {target!r}")

    GEN.mkdir(parents=True, exist_ok=True)
    SRC.mkdir(parents=True, exist_ok=True)
    grid = load_grid()

    #--- ground -------------------------------------------------------------
    world, coll, hmap = build_world(grid)
    write_png(world, BG_GROUND, SRC / "world_preview.png", transparent0=False)
    bg_chr, bg_map, nchars = dedupe_tilemap(world)
    if nchars > 512:
        raise SystemExit(f"ground needs {nchars} characters; BG1 holds 512. "
                         f"Simplify the terrain shading.")
    write_bin(GEN / "bgchr.bin", bg_chr)
    write_bin(GEN / "bg1map.bin", bg_map)
    write_bin(GEN / "collmap.bin", coll)
    write_bin(GEN / "heightmap.bin", hmap)
    write_bin(GEN / "bgpal.bin", palette_bytes(BG_GROUND) + bytes(256 - 32))
    # The same tiles, the same tilemap, one different palette: that is the
    # whole of the night version of the island.
    write_bin(GEN / "nightpal.bin", palette_bytes(BG_NIGHT) + bytes(256 - 32))
    write_png(world, BG_NIGHT, SRC / "night_preview.png", transparent0=False)

    #--- the last piece of it ----------------------------------------------
    frag_grid = load_grid("fragment.txt")
    frag, frag_coll, frag_hmap = build_world(frag_grid)
    write_png(frag, BG_NIGHT, SRC / "fragment_preview.png", transparent0=False)
    frag_chr, frag_map, frag_n = dedupe_tilemap(frag)
    if frag_n > 512:
        raise SystemExit(f"the fragment needs {frag_n} characters; BG1 holds 512.")
    write_bin(GEN / "fragchr.bin", frag_chr)
    write_bin(GEN / "fragmap.bin", frag_map)
    write_bin(GEN / "fragcoll.bin", frag_coll)
    write_bin(GEN / "fragheight.bin", frag_hmap)
    print(f"fragment  {frag_n:3d} unique characters, {len(frag_chr):5d} bytes chr, "
          f"{sum(frag_coll):3d} walkable")

    #--- Traverse Town -----------------------------------------------------
    write_bin(GEN / "townpal.bin", palette_bytes(BG_TOWN) + bytes(256 - 32))
    for n, stem in ((1, "town1"), (2, "town2"), (3, "town3")):
        grid = load_grid(f"{stem}.txt")
        canvas, coll, hmap = build_world(grid)
        write_png(canvas, BG_TOWN, SRC / f"{stem}_preview.png", transparent0=False)
        chr_, map_, count = dedupe_tilemap(canvas)
        if count > 512:
            raise SystemExit(f"{stem} needs {count} characters; BG1 holds 512.")
        write_bin(GEN / f"{stem}chr.bin", chr_)
        write_bin(GEN / f"{stem}map.bin", map_)
        write_bin(GEN / f"{stem}coll.bin", coll)
        write_bin(GEN / f"{stem}height.bin", hmap)
        print(f"{stem}     {count:3d} unique characters, {len(chr_):5d} bytes chr, "
              f"{sum(coll):3d} walkable")

    #--- Station of Awakening ----------------------------------------------
    dive, dive_coll = build_dive_platform()
    write_png(dive, BG_DIVE, SRC / "dive_preview.png", transparent0=False)
    dive_chr, dive_map, dive_n = dedupe_tilemap(dive)
    if dive_n > 512:
        raise SystemExit(f"the platform needs {dive_n} characters; BG1 holds "
                         f"512. Simplify the glass.")
    write_bin(GEN / "divechr.bin", dive_chr)
    write_bin(GEN / "divemap.bin", dive_map)
    write_bin(GEN / "divecoll.bin", dive_coll)
    write_bin(GEN / "divepal.bin", palette_bytes(BG_DIVE) + bytes(256 - 32))

    dive2, dive2_coll = build_dive_platform(station=2)
    write_png(dive2, BG_DIVE, SRC / "dive2_preview.png", transparent0=False)
    d2_chr, d2_map, d2_n = dedupe_tilemap(dive2)
    if d2_n > 512:
        raise SystemExit(f"station two needs {d2_n} characters; BG1 holds 512.")
    write_bin(GEN / "dive2chr.bin", d2_chr)
    write_bin(GEN / "dive2map.bin", d2_map)
    write_bin(GEN / "dive2coll.bin", dive2_coll)

    dive3, dive3_coll = build_dive_platform(station=3)
    write_png(dive3, BG_DIVE, SRC / "dive3_preview.png", transparent0=False)
    d3_chr, d3_map, d3_n = dedupe_tilemap(dive3)
    if d3_n > 512:
        raise SystemExit(f"station three needs {d3_n} characters; BG1 holds 512.")
    write_bin(GEN / "dive3chr.bin", d3_chr)
    write_bin(GEN / "dive3map.bin", d3_map)
    write_bin(GEN / "dive3coll.bin", dive3_coll)

    #--- Sora ---------------------------------------------------------------
    sheet = build_sora()
    write_png(sheet, OBJ_SORA, SRC / "sora.png")
    write_bin(GEN / "sorachr.bin", sora_stream_order(sheet))

    #--- object pages -------------------------------------------------------
    page = build_obj_page()
    write_png(page, OBJ_SCENE, SRC / "obj_preview.png")
    write_bin(GEN / "objchr.bin", encode_4bpp_page(page))

    page2 = build_obj_page2()
    write_png(page2, OBJ_ISLE, SRC / "obj2_preview.png")
    write_bin(GEN / "obj2chr.bin", encode_4bpp_page(page2))

    # The same VRAM page, loaded with the town's cast instead while it is up.
    town_page = build_obj_town()
    write_png(town_page, OBJ_TOWN, SRC / "objtown_preview.png")
    write_bin(GEN / "objtownchr.bin", encode_4bpp_page(town_page))
    write_bin(GEN / "townobjpal.bin",
              palette_bytes(OBJ_TOWN) + palette_bytes(OBJ_ARMOR))

    obj_pal = (palette_bytes(OBJ_SORA) + palette_bytes(OBJ_HEART) +
               palette_bytes(OBJ_SCENE) + palette_bytes(OBJ_FX) +
               palette_bytes(OBJ_SHADOW) + palette_bytes(OBJ_DIVE))
    obj_pal += bytes(256 - len(obj_pal))
    write_bin(GEN / "objpal.bin", obj_pal)
    # Loaded over OBJ palette 1 while the island is falling: the Shadows take
    # the three colours the absent islanders were using.
    write_bin(GEN / "nightobjpal.bin",
              palette_bytes(OBJ_NIGHT) + palette_bytes(OBJ_SCENE_NIGHT))
    # Loaded over OBJ palette 1 while the island is up.
    write_bin(GEN / "islepal.bin", palette_bytes(OBJ_ISLE))

    #--- HUD ----------------------------------------------------------------
    font = build_hud_font()
    write_png(font, HUD_PAL, SRC / "hud.png")
    write_bin(GEN / "hudchr.bin", encode_2bpp_page(font))
    # 2bpp palette 4 lands on CGRAM 16-19, clear of BG palette 0
    # where all sixteen ground colours live.
    write_bin(GEN / "hudpal.bin", palette_bytes(HUD_PAL, 4))

    print(f"station1  {dive_n:3d} unique characters, "
          f"{len(dive_chr):5d} bytes chr")
    print(f"station2  {d2_n:3d} unique characters, "
          f"{len(d2_chr):5d} bytes chr")
    print(f"station3  {d3_n:3d} unique characters, "
          f"{len(d3_chr):5d} bytes chr")
    print(f"ground    {nchars:3d} unique characters, "
          f"{len(bg_chr):5d} bytes chr, {len(bg_map)} bytes map")
    print(f"sora      {len(sheet.px[0])//32}x{len(sheet.px)//32} cels, "
          f"{Path(GEN / 'sorachr.bin').stat().st_size:5d} bytes "
          f"({Path(GEN / 'sorachr.bin').stat().st_size // 512} frames)")
    print(f"objects   {Path(GEN / 'objchr.bin').stat().st_size:5d} bytes"
          f" + {Path(GEN / 'obj2chr.bin').stat().st_size} on page two")
    print(f"hud       {Path(GEN / 'hudchr.bin').stat().st_size:5d} bytes")
    walkable = sum(coll)
    print(f"collision {walkable} walkable of {MAP_W * MAP_H} tiles")

    #--- the Nintendo DS worlds ---------------------------------------------
    # Larger than anything the SNES can address, so they live beside the frozen
    # maps rather than replacing them: the SNES build and the oracle keep using
    # assets/*.txt untouched, which is what holds its ROM byte-identical.
    # The palette is per-scene: the town reuses the island's sixteen slots with
    # different colours in them, so a preview drawn with the wrong one is
    # unreadable even though the emitted indices are right.  And the night reuses
    # the island's ground with nothing changed but those sixteen colours, which
    # is what ds_scenes() exists to express.
    if target == "snes":
        print("ds/       skipped (--target snes)")
        return 0
    if target == "ds":
        print("ds/       (the SNES artefacts above were regenerated as a "
              "by-product; see main.__doc__)")
    shapes = [build_ds_scene(scene) for scene in ds_scenes()]
    sprites = build_ds_sprites(sheet, page, page2, town_page, font)
    palettes = build_ds_palettes()
    emit_ds_asset_header(shapes, sprites, palettes)
    print(f"ds/       {len(sprites)} sprite pages, {len(palettes)} palettes, "
          f"platform/ds/include/gen/assets.h")
    return 0


if __name__ == "__main__":
    sys.exit(main())
