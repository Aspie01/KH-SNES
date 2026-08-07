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
    BG_GROUND, HUD_PAL, OBJ_FX, OBJ_HEART, OBJ_SCENE, OBJ_SHADOW, OBJ_SORA,
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


def build_obj_page() -> Canvas:
    """Assemble the 128x128 sprite page (a 16x16 grid of 8x8 tiles).

    Tiles $00-$3F are left empty: that block is the window Sora's current
    animation frame is streamed into every time it changes.
    """
    page = Canvas(128, 128)
    page.blit(draw_palm(), 32, 0)           # tile $04
    page.blit(draw_boulder(), 64, 0)        # tile $08
    page.blit(draw_shadow_big(), 96, 0)     # tile $0C
    for f in range(4):                      # tiles $40, $42, $44, $46
        page.blit(draw_heartless(f), f * 16, 32)
    page.blit(draw_shadow_blob(), 64, 32)   # tile $48
    page.blit(draw_slash(0), 80, 32)        # tile $4A
    page.blit(draw_slash(1), 96, 32)        # tile $4C
    page.blit(draw_rock(), 112, 32)         # tile $4E
    return page


# ---------------------------------------------------------------------------
# HUD font (2bpp)
# ---------------------------------------------------------------------------

GLYPHS = {
    "A": ("..##..", ".#..#.", "#....#", "######", "#....#", "#....#"),
    "H": ("#....#", "#....#", "######", "#....#", "#....#", "#....#"),
    "M": ("#....#", "##..##", "#.##.#", "#....#", "#....#", "#....#"),
    "P": ("#####.", "#....#", "#####.", "#.....", "#.....", "#....."),
}


def build_hud_font() -> Canvas:
    """One 8x8 tile per character; 128 tiles wide enough for the whole set."""
    page = Canvas(8 * 16, 8 * 8)           # 16x8 tiles = 128 tiles

    def put(tile_index: int, rows: list[str], color: int = 1) -> None:
        tx, ty = (tile_index % 16) * 8, (tile_index // 16) * 8
        for y, row in enumerate(rows):
            for x, ch in enumerate(row):
                if ch == "#":
                    page.set(tx + x + 1, ty + y + 1, color)

    # Tiles 1..26 are A..Z; only the letters the HUD actually shows are drawn.
    for letter, rows in GLYPHS.items():
        put(1 + ord(letter) - ord("A"), list(rows))

    # Digits 0-9 at tiles 27..36, drawn as simple 5x6 forms.
    digits = {
        "0": ("####", "#..#", "#..#", "#..#", "#..#", "####"),
        "1": ("..#.", ".##.", "..#.", "..#.", "..#.", "####"),
        "2": ("####", "...#", "####", "#...", "#...", "####"),
        "3": ("####", "...#", "####", "...#", "...#", "####"),
        "4": ("#..#", "#..#", "####", "...#", "...#", "...#"),
        "5": ("####", "#...", "####", "...#", "...#", "####"),
        "6": ("####", "#...", "####", "#..#", "#..#", "####"),
        "7": ("####", "...#", "...#", "..#.", ".#..", ".#.."),
        "8": ("####", "#..#", "####", "#..#", "#..#", "####"),
        "9": ("####", "#..#", "####", "...#", "...#", "####"),
    }
    for d, rows in digits.items():
        put(27 + int(d), list(rows))

    put(37, ["...#", "...#", "..#.", ".#..", "#...", "#..."])   # '/'

    # Gauge pieces: left cap, full, half, empty, right cap.
    def bar(tile_index: int, fill_cols: int, cap: str | None = None) -> None:
        tx, ty = (tile_index % 16) * 8, (tile_index // 16) * 8
        for y in range(2, 7):
            for x in range(8):
                page.set(tx + x, ty + y, 2)
        for y in range(3, 6):
            for x in range(fill_cols):
                page.set(tx + x, ty + y, 1)
        for x in range(8):
            page.set(tx + x, ty + 1, 3)
            page.set(tx + x, ty + 7, 3)
        if cap == "L":
            for y in range(1, 8):
                page.set(tx, ty + y, 3)
        elif cap == "R":
            for y in range(1, 8):
                page.set(tx + 7, ty + y, 3)

    bar(40, 0, "L")
    bar(41, 8)
    bar(42, 4)
    bar(43, 0)
    bar(44, 0, "R")
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
               palette_bytes(OBJ_SHADOW))
    obj_pal += bytes(256 - len(obj_pal))
    write_bin(GEN / "objpal.bin", obj_pal)

    #--- HUD ----------------------------------------------------------------
    font = build_hud_font()
    write_png(font, HUD_PAL, SRC / "hud.png")
    write_bin(GEN / "hudchr.bin", encode_2bpp_page(font))
    # 2bpp palette 4 lands on CGRAM 16-19, clear of BG palette 0
    # where all sixteen ground colours live.
    write_bin(GEN / "hudpal.bin", palette_bytes(HUD_PAL, 4))

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
