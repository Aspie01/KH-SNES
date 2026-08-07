#!/usr/bin/env python3
"""Generate every art and data asset the ROM links against.

Outputs into assets/gen (binaries the assembler includes) and assets/src
(indexed PNGs, so the art can be inspected or hand-redrawn and re-imported).

All artwork here is original.  Nothing is traced, ripped, or imported from a
commercial release; the palettes and proportions are written from scratch to
sit inside SNES limits (16 colours per palette, 4bpp tiles, 256-tile pages).
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from pixel import (                                    # noqa: E402
    BG_DIVE, BG_GROUND, HUD_PAL, OBJ_DIVE, OBJ_FX, OBJ_HEART, OBJ_SCENE,
    OBJ_SHADOW, OBJ_SORA,
    Canvas, GEN, ROOT, SRC, cut_tiles, encode_2bpp_page, encode_4bpp_page,
    palette_bytes, tile_4bpp, write_bin, write_png,
)

MAP_W = MAP_H = 16
ORIGIN_X = (MAP_H - 1) * 16
TILEMAP_W, TILEMAP_H = 64, 32           # PPU tilemap, in 8x8 characters
WORLD_W, WORLD_H = TILEMAP_W * 8, TILEMAP_H * 8

# Ground palette indices, named for legibility below.
SAND_L, SAND_M, SAND_D = 1, 2, 3
GRASS_L, GRASS_M, GRASS_D = 4, 5, 6
WATER_L, WATER_M, WATER_D = 7, 8, 9
WOOD_L, WOOD_D = 10, 11
ROCK_L, ROCK_D = 12, 13
FOAM, OUTLINE = 14, 15

# terrain code -> (light, mid, dark, walkable)
TERRAIN = {
    "~": (WATER_M, WATER_D, WATER_D, False),
    "-": (WATER_L, WATER_M, WATER_D, False),
    ".": (SAND_L, SAND_M, SAND_D, True),
    ",": (GRASS_L, GRASS_M, GRASS_D, True),
    "=": (WOOD_L, WOOD_D, WOOD_D, True),
    "#": (ROCK_L, ROCK_D, ROCK_D, False),
    "T": (GRASS_L, GRASS_M, GRASS_D, False),
    "R": (GRASS_L, GRASS_M, GRASS_D, False),
    "r": (SAND_L, SAND_M, SAND_D, False),
}


# ---------------------------------------------------------------------------
# Isometric ground
# ---------------------------------------------------------------------------

def diamond_span(y: int, h: int = 16, w: int = 32) -> tuple[int, int]:
    """Half-open [x0, x1) span of a 32x16 isometric diamond at row y."""
    hw = 2 * (y + 1) if y < h // 2 else 2 * (h - y)
    return w // 2 - hw, w // 2 + hw


# Tiles only need a rim where they meet a *different* surface.  Drawing the
# bevel unconditionally turns the ground into a visible lattice, which is the
# usual way isometric tiling gives itself away.
GROUP = {"~": "water", "-": "water", ".": "sand", "r": "sand",
         ",": "grass", "T": "grass", "R": "grass", "=": "wood", "#": "rock"}

SAND_SPECKLE = (((11, 6), SAND_L), ((20, 9), SAND_L), ((15, 11), SAND_L),
                ((9, 9), SAND_L), ((18, 5), SAND_D), ((13, 10), SAND_D))
GRASS_SPECKLE = (((10, 7), GRASS_D), ((19, 6), GRASS_D), ((14, 10), GRASS_D),
                 ((22, 9), GRASS_D), ((16, 5), GRASS_L), ((12, 9), GRASS_L))


def draw_diamond(code: str, phase: int, edges: dict[str, str | None]) -> Canvas:
    """One 32x16 ground tile, aware of what it borders on each of its edges."""
    light, mid, dark, _ = TERRAIN[code]
    me = GROUP[code]
    c = Canvas(32, 16)

    for y in range(16):
        x0, x1 = diamond_span(y)
        for x in range(x0, x1):
            c.set(x, y, mid)

    if code in "~-":
        # Drifting highlights so open water is not a flat field.
        if phase == 0:
            c.hline(12, 19, 5, WATER_L)
            c.hline(10, 14, 9, WATER_L)
        else:
            c.hline(13, 18, 4, WATER_L)
            c.hline(16, 21, 10, WATER_L)
    elif code in ".r":
        for (tx, ty), col in SAND_SPECKLE:
            c.set(tx, ty, col)
    elif code in ",TR":
        for (tx, ty), col in GRASS_SPECKLE:
            c.set(tx, ty, col)
    elif code == "=":
        for y in range(0, 16, 4):
            x0, x1 = diamond_span(y)
            for x in range(x0, x1):
                c.set(x, y, WOOD_D)
    elif code == "#":
        for y in range(3, 13, 3):
            x0, x1 = diamond_span(y)
            for x in range(x0 + 2, x1 - 2, 3):
                c.set(x, y, ROCK_D)

    def edge_colour(neighbour: str | None, upper: bool) -> int | None:
        if neighbour is None:
            return None
        other = GROUP[neighbour]
        if other == me:
            return None
        if other == "water" and me != "water":
            return FOAM                 # a shoreline, not just a seam
        return light if upper else dark

    nw = edge_colour(edges["nw"], True)
    ne = edge_colour(edges["ne"], True)
    sw = edge_colour(edges["sw"], False)
    se = edge_colour(edges["se"], False)

    for y in range(16):
        x0, x1 = diamond_span(y)
        left, right = (nw, ne) if y < 8 else (sw, se)
        if left is not None:
            c.set(x0, y, left)
            c.set(x0 + 1, y, left)
        if right is not None:
            c.set(x1 - 1, y, right)
            c.set(x1 - 2, y, right)

    return c


def build_world(grid: list[str]) -> tuple[Canvas, bytes]:
    """Paint the island, back to front, and derive the collision map."""
    world = Canvas(WORLD_W, WORLD_H, WATER_D)

    def at(i: int, j: int) -> str | None:
        if 0 <= i < MAP_W and 0 <= j < MAP_H:
            return grid[j][i]
        return None

    order = sorted(((i, j) for j in range(MAP_H) for i in range(MAP_W)),
                   key=lambda t: t[0] + t[1])
    for i, j in order:
        code = grid[j][i]
        wx = (i - j) * 16 + ORIGIN_X
        wy = (i + j) * 8
        # On screen +i runs down-right and +j runs down-left, so the four
        # neighbours land on the diamond's four edges.
        edges = {"nw": at(i - 1, j), "ne": at(i, j - 1),
                 "se": at(i + 1, j), "sw": at(i, j + 1)}
        tile = draw_diamond(code, (i + j) % 2, edges)
        # Skip index 0: a diamond's 32x16 bounding box has empty corners that
        # sit on top of the neighbours already painted there, and copying them
        # punches the bottom half out of every tile behind this one.
        world.blit(tile, wx, wy, transparent=0)

    coll = bytearray(MAP_W * MAP_H)
    for j in range(MAP_H):
        for i in range(MAP_W):
            coll[j * MAP_W + i] = 1 if TERRAIN[grid[j][i]][3] else 0
    return world, bytes(coll)


def dedupe_tilemap(world: Canvas) -> tuple[bytes, bytes, int]:
    """Slice the painted world into 8x8 characters and fold duplicates.

    Matching is done against horizontal, vertical and both flips, since the
    tilemap carries a flip bit for each axis -- on symmetric isometric art
    that roughly halves the character count.
    """
    chars: list[bytes] = []
    index: dict[bytes, tuple[int, int]] = {}
    entries: list[int] = [0] * (TILEMAP_W * TILEMAP_H)

    for ty in range(TILEMAP_H):
        for tx in range(TILEMAP_W):
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

            # A 64x32 tilemap is NOT stored as 32 rows of 64 entries: the PPU
            # keeps it as two 32x32 screens laid side by side, the left screen
            # first.  Writing it row-major shreds the map into diagonal bands.
            screen = tx // 32
            entries[screen * 1024 + ty * 32 + (tx % 32)] = hit[0] | hit[1]

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


def draw_heartless(frame: int) -> Canvas:
    """A Shadow: crouched, twitching antennae, two yellow eyes."""
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
# Sized so most of the platform is on screen at once: the SNES cannot pull
# the camera back, so the platform has to come to it.
DIVE_RX, DIVE_RY = 168.0, 84.0      # a circle seen at 2:1

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


def dive_medallion(r: float, ang: float, nx: float, ny: float,
                   station: int = 1) -> int | None:
    """The figure at the centre of the platform, in un-squashed coordinates.

    nx/ny are -1..1 across the medallion with the isometric squash undone, so
    the drawing below is composed as if seen head-on and comes out foreshortened
    on the platform, which is how the stations read in the source game.
    """
    # Pale radiating backdrop.
    base = G_PALE if int((ang / (2 * math.pi)) * 24) % 2 == 0 else G_WHITE
    # Station two dresses the same figure differently: fair hair, blue gown.
    hair = G_HAIR if station == 1 else G_GOLD
    hair_hi = G_HAIR if station == 1 else G_PALE
    bow = G_RED if station == 1 else G_LIGHT
    bodice_a = G_MID if station == 1 else G_LIGHT
    bodice_b = G_DEEP if station == 1 else G_MID
    skirt_a = G_PALE if station == 1 else G_WHITE
    skirt_b = G_GOLD if station == 1 else G_LIGHT
    sleeve = G_RED_D if station == 1 else G_WHITE

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


def build_dive_platform(station: int = 1) -> tuple[Canvas, bytes]:
    """Paint the platform and derive which isometric tiles are standable."""
    world = Canvas(WORLD_W, WORLD_H, V_VOID)

    for y in range(WORLD_H):
        for x in range(WORLD_W):
            dx = (x + 0.5 - DIVE_CX) / DIVE_RX
            dy = (y + 0.5 - DIVE_CY) / DIVE_RY
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
                wedges = WEDGE_COLOURS if station == 1 else WEDGE_COLOURS_2
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
            wx = (i - j) * 16 + ORIGIN_X + 16
            wy = (i + j) * 8 + 8
            dx = (wx - DIVE_CX) / (DIVE_RX - 18)
            dy = (wy - DIVE_CY) / (DIVE_RY - 9)
            coll[j * MAP_W + i] = 1 if dx * dx + dy * dy <= 1.0 else 0
    return world, bytes(coll)


# --- pedestals and the three dream weapons --------------------------------

D_OUT, D_STONE_L, D_STONE_M, D_STONE_D = 1, 2, 3, 4
D_MET_L, D_MET_M, D_MET_D = 5, 6, 7
D_GOLD, D_GOLD_D, D_RED, D_RED_D = 8, 9, 10, 11
D_BLUE, D_BLUE_D, D_GLOW, D_WOOD = 12, 13, 14, 15


def draw_pedestal() -> Canvas:
    """A short isometric dais for a weapon to hover over."""
    c = Canvas(32, 32)
    # column
    c.rect(10, 18, 21, 27, D_STONE_D)
    c.rect(10, 18, 15, 27, D_STONE_M)
    # top slab, an isometric diamond
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

def load_grid() -> list[str]:
    text = (ROOT / "assets" / "island.txt").read_text().splitlines()
    rows = [ln for ln in text if ln and not ln.startswith("#")]
    if len(rows) != MAP_H:
        raise SystemExit(f"island.txt: expected {MAP_H} map rows, got {len(rows)}")
    for n, row in enumerate(rows):
        if len(row) != MAP_W:
            raise SystemExit(f"island.txt row {n}: expected {MAP_W} columns, "
                             f"got {len(row)}")
        for ch in row:
            if ch not in TERRAIN:
                raise SystemExit(f"island.txt row {n}: unknown terrain '{ch}'")
    return rows


def main() -> int:
    GEN.mkdir(parents=True, exist_ok=True)
    SRC.mkdir(parents=True, exist_ok=True)
    grid = load_grid()

    #--- ground -------------------------------------------------------------
    world, coll = build_world(grid)
    write_png(world, BG_GROUND, SRC / "world_preview.png", transparent0=False)
    bg_chr, bg_map, nchars = dedupe_tilemap(world)
    if nchars > 256:
        raise SystemExit(f"ground needs {nchars} characters; the 8 KiB BG page "
                         f"holds 256. Simplify the terrain shading.")
    write_bin(GEN / "bgchr.bin", bg_chr)
    write_bin(GEN / "bg1map.bin", bg_map)
    write_bin(GEN / "collmap.bin", coll)
    write_bin(GEN / "bgpal.bin", palette_bytes(BG_GROUND) + bytes(256 - 32))

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

    #--- Sora ---------------------------------------------------------------
    sheet = build_sora()
    write_png(sheet, OBJ_SORA, SRC / "sora.png")
    write_bin(GEN / "sorachr.bin", sora_stream_order(sheet))

    #--- object page --------------------------------------------------------
    page = build_obj_page()
    write_png(page, OBJ_SCENE, SRC / "obj_preview.png")
    write_bin(GEN / "objchr.bin", encode_4bpp_page(page))

    obj_pal = (palette_bytes(OBJ_SORA) + palette_bytes(OBJ_HEART) +
               palette_bytes(OBJ_SCENE) + palette_bytes(OBJ_FX) +
               palette_bytes(OBJ_SHADOW) + palette_bytes(OBJ_DIVE))
    obj_pal += bytes(256 - len(obj_pal))
    write_bin(GEN / "objpal.bin", obj_pal)

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
    print(f"ground    {nchars:3d} unique characters, "
          f"{len(bg_chr):5d} bytes chr, {len(bg_map)} bytes map")
    print(f"sora      {len(sheet.px[0])//32}x{len(sheet.px)//32} cels, "
          f"{Path(GEN / 'sorachr.bin').stat().st_size:5d} bytes "
          f"({Path(GEN / 'sorachr.bin').stat().st_size // 512} frames)")
    print(f"objects   {Path(GEN / 'objchr.bin').stat().st_size:5d} bytes")
    print(f"hud       {Path(GEN / 'hudchr.bin').stat().st_size:5d} bytes")
    walkable = sum(coll)
    print(f"collision {walkable} walkable of {MAP_W * MAP_H} tiles")
    return 0


if __name__ == "__main__":
    sys.exit(main())
