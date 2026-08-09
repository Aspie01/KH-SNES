#!/usr/bin/env python3
"""Every constant in game.inc, checked against the DS port BY VALUE.

    python3 tools/check_constants.py

§M1's brief is "every constant from `game.inc` as `constexpr`, with the SNES name
preserved in a comment so a reader can cross-reference".  That was done, and
nothing checked it afterwards -- so the guarantee decayed silently, which is the
only way a table of numbers ever fails.  Eight constants had gone missing by the
time this was written: the four world positions the race is measured from and to
(`START_SORA_*`, `START_RIKU_*`, `PAOPU_*`, `FINISH_*`), which had been inlined
as `tileCentre(27)` at their one call site and named nowhere.

WHY BY VALUE AND NOT BY NAME.  A missing constant is the cheap failure: the build
breaks, or a reviewer notices.  The expensive one is a constant that is PRESENT
AND WRONG -- 44 where the assembly says 40 -- because it produces a game that
plays almost right and a trace that diverges four hundred frames later.  So this
parses the number out of both sides and compares them.

WHAT IT CANNOT DO is read C++.  It does not need to: `constants.h` carries the
SNES name in a comment beside each value precisely so that a reader -- human or
this -- can find it, and the ENUM families are matched by their position in an
`enum class` instead.  Anything it genuinely cannot follow goes in EXCUSED below
WITH A REASON, and an excuse that stops being needed is itself an error, because
a stale exclusion list is how a checker quietly stops checking.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# BOTH include files, and the second one is the point of this paragraph.
#
# This tool read game.inc and only game.inc for its whole existence, and
# text.inc -- the other half of the specification's numbers, the font's glyph
# indices and the dialogue box's geometry -- was checked by NOTHING.  Twenty-three
# of its forty-eight constants had never reached the DS at all: every gauge
# glyph, the entire nine-patch the dialogue window is drawn from, CH_CLEAR, and
# all seven box and menu geometry numbers.
#
# It is obvious in hindsight why they were missed and why nothing noticed.  §M5
# ported the dialogue INTERPRETER and did it carefully -- SC_END, TS_REVEAL,
# TM_RAFT, TEXT_W and TEXT_H are all present and correct -- and left rendering
# to §M7.  So the file looked ported.  The half that was missing was the half
# nothing needed yet, which is exactly the half a checker is for.
INCS = (ROOT / "platform" / "snes" / "src" / "game.inc",
        ROOT / "platform" / "snes" / "src" / "text.inc")
DS = ROOT / "platform" / "ds" / "include"

# The enum-shaped families.  The SNES numbers them by hand and the DS uses an
# `enum class`, so the check is: the DS enumerator exists, and its value is the
# SNES's.  Enumerators without an explicit `= n` take the previous plus one,
# which is exactly what the assembly's hand-numbering does too.
FAMILIES = {
    "ACT_": ("ActType", "actor.h"),
    "IT_": ("Item", "constants.h"),
    "SCENE_": ("SceneId", "constants.h"),
    "WEAPON_": ("Weapon", "constants.h"),
    "DIVE_": ("DiveStage", "constants.h"),
    "DSS_": ("BossState", "constants.h"),
    "ST_": ("ActState", "actor.h"),
    "DIR_": ("Dir", "constants.h"),
    "Q_": ("QuestState", "constants.h"),
    "N_": ("NightStage", "constants.h"),
    "T_": ("TownStage", "constants.h"),
    "GAS_": ("ArmorState", "constants.h"),
    "RAFT_": ("RaftName", "constants.h"),
    "AF_": ("ActFlags", "actor.h"),
    # text.inc's two, which are enum classes here for the same reason the
    # others are: a txtState cannot be compared against a txtMode.
    "TS_": ("TextState", "text.h"),
    "TM_": ("TextMode", "text.h"),
}

# The two families the DS keeps as a NAMESPACE of constants rather than an enum,
# because they are opaque identifiers -- a tile number and a sub-palette -- and
# an enum class would invite arithmetic on them.
NAMESPACES = {
    "TILE_": ("sprite", "constants.h"),
    "PAL_OBJ_": ("pal", "constants.h"),
}

# Prefixes that LOOK like a family and are not.  DIVE_CAM_X is a camera bound,
# not a stage; without this it would be looked up as DiveStage::CamX.
NOT_A_FAMILY = ("DIVE_CAM_X", "DIVE_CAM_Y", "DIVE_CX", "DIVE_CY",
                "DIVE_RX", "DIVE_RY", "DIVE_R", "DIVE_INSET",
                "TILE_PX", "TILE_SHIFT",
                "ACT_STRIDE", "IT_COUNT", "T_COUNT")

# SNES enumerator -> DS enumerator, where the two spell it differently.  Only
# the ones that are not just CamelCase of the tail.
RENAMED = {
    "ACT_ROCKBIG": "RockBig", "ACT_PALMC": "PalmC", "ACT_DOOROPEN": "DoorOpen",
    "ACT_TOWNMAN": "TownMan", "ACT_TOWNWOMAN": "TownWoman",
    "IT_NUT": "Nut", "IT_WATER": "Water",
    "SCENE_DIVE2": "Dive2", "SCENE_DIVE3": "Dive3",
    "SCENE_TOWN1": "Town1", "SCENE_TOWN2": "Town2", "SCENE_TOWN3": "Town3",
    "DIVE_S2_INTRO": "S2Intro", "DIVE_S2_FIGHT": "S2Fight",
    "DIVE_SHATTER2": "Shatter2", "DIVE_S3_INTRO": "S3Intro",
    "DSS_SLAM_UP": "SlamUp", "DSS_SLAM_HIT": "SlamHit",
    "DSS_ORB_UP": "OrbUp", "DSS_ORB_FIRE": "OrbFire",
    "DSS_SWEEP_UP": "SweepUp", "DSS_SWEEP_HIT": "SweepHit",
    "Q_DAYOUT": "DayOut", "Q_DAYIN": "DayIn", "Q_RACE_SET": "RaceSet",
    "Q_RACE_RUN": "RaceRun", "Q_RACE_OVER": "RaceOver",
    "AF_HFLIP": "HFlip",
    # The sprite ids, where CamelCase of the tail is not what the DS calls it.
    "TILE_ROCKBIG": "RockBig", "TILE_SHADOWBIG": "ShadowBig",
    "TILE_DOOROPEN": "DoorOpen", "TILE_TOWNMAN": "TownMan",
    "TILE_TOWNWOMAN": "TownWoman", "TILE_HEART_NIGHT": "HeartNight",
    # Dir keeps the compass in capitals, because SE is a bearing and not a word.
    "DIR_S": "S", "DIR_SE": "SE", "DIR_E": "E", "DIR_NE": "NE",
    "DIR_N": "N", "DIR_NW": "NW", "DIR_W": "W", "DIR_SW": "SW",
}

# Constants the DS DERIVES rather than names, with the derivation.  The Shadow's
# four cels sit two tiles apart from a base the scene supplies -- world.cpp does
# `anim * 2 + heartTile` -- so naming the other three would be three chances to
# disagree with the arithmetic.  Checked against the formula anyway, because
# "derived" is only an excuse if the derivation is right.
DERIVED = {
    "TILE_HEART1": ("sprite", "Heart0", 2),
    "TILE_HEART2": ("sprite", "Heart0", 4),
    "TILE_HEART3": ("sprite", "Heart0", 6),
}

# The raft's shopping list.  game.inc names each requirement; the DS holds them
# as NEED[], indexed by Item, so the check is against the array's element.
NEED_INDEX = {
    "NEED_LOGS": 0, "NEED_CLOTH": 1, "NEED_ROPE": 2, "NEED_MUSH": 3,
    "NEED_NUT": 4, "NEED_EGG": 5, "NEED_WATER": 6, "NEED_FISH": 7,
}

# Anything this cannot or should not follow.  EVERY ENTRY CARRIES A REASON, and
# an entry that is no longer needed fails the run.
EXCUSED = {
    "GAME_INC": "the include guard, not a constant",
    "TXT_ATTR": "a SNES BG3 map attribute -- priority bit plus palette 4 in a "
                "three-bit field.  The DS packs a map entry differently (palette "
                "in bits 12-15, no priority bit: priority is per LAYER in "
                "BGxCNT), so there is no number to carry across.  See "
                "device/hud.cpp, which builds the DS's equivalent.",
    "TEXT_INC": "the include guard, not a constant",
    "MAP_W_SHIFT": "log2 of a fixed map width; DS map size is per scene and "
                   "SceneGround indexes with a multiply instead",
    # The SNES PPU.  vram_map.h is the DS's answer to all of these and it does
    # not resemble them, because the hardware does not.
    **{n: "an SNES PPU register or VRAM address; see platform/ds/include/vram_map.h"
       for n in ("VRAM_BG1_CHR", "VRAM_BG3_CHR", "VRAM_BG1_MAP", "VRAM_BG3_MAP",
                 "VRAM_OBJ_CHR", "VRAM_OBJ2_CHR", "BG1SC_VAL", "BG3SC_VAL",
                 "BG12NBA_VAL", "BG34NBA_VAL", "OBSEL_VAL", "BGMODE_VAL",
                 "TM_VAL", "TS_VAL", "CGWSEL_VAL", "CGADSUB_VAL")},
    "SORA_ROW_BYTES": "the SNES's streaming window is four tiles wide in a "
                      "16-tile-wide VRAM page; the DS streams whole cels",
    "SORA_ROW_STRIDE": "...and this is that page's stride, for the same reason",
    # The dead five, which §M1 was told to leave out on purpose.
    **{n: "dead on the SNES too -- one reference, its own definition.  "
          "docs/BEHAVIOUR.md section 11"
       for n in ("ORB_SPEED", "LINE_X", "LINE_Y", "AF_SOLID")},
}

# The SNES spells a few plain constants differently.  Value still checked.
ALIASES = {
    "SORA_FRAME_BYTES": "SORA_CEL_BYTES",
    "MAP_W": "ORACLE_MAP_W",
    "MAP_H": "ORACLE_MAP_H",
    "SHADOW_MAX": "SNES_SHADOW_MAX",
    "SHADOW_GAP": "SNES_SHADOW_GAP",
    "NIGHT_SPOTS": "SNES_NIGHT_SPOTS",
    "DIVE_RX": "SNES_DIVE_R",
    "DIVE_RY": "SNES_DIVE_R",
    "SCREEN_H": "SNES_SCREEN_H",
    "MAX_ACTORS": "SNES_MAX_ACTORS",
    "DIVE_CX": "DIVE_CX",
    "DIVE_CY": "DIVE_CY",
}

# Numbers the DS deliberately holds DIFFERENTLY, each naming the divergence that
# says why.  These are the entries worth reading: a value that changed on purpose
# is indistinguishable from one that changed by accident unless somebody wrote
# down which.
DIVERGED = {
    "SCREEN_H": "001 -- the DS is 256x192; SNES_SCREEN_H keeps the old one",
    "CAM_MAX_Y": "001 -- 32 lines shorter screen, so 64 of slack and not 32",
    "DIVE_CAM_Y": "001 -- the station's pin moves with the shorter screen",
    "DIVE_RX": "004 -- the disc is drawn at radius 92; SNES_DIVE_R keeps 110",
    "DIVE_RY": "004 -- the same, on the other axis",
    "SHADOW_MAX": "003 -- the night's island is four times the area",
    "SHADOW_GAP": "003 -- and the fill should still take a crossing",
    "NIGHT_SPOTS": "003 -- four times the ground needs four times the places",
    "MAX_ACTORS": "002 -- the pool holds 128; SNES_MAX_ACTORS keeps 32",
}


def snes_constants() -> list[tuple[str, str]]:
    out = []
    seen: dict[str, str] = {}
    for inc in INCS:
        for m in re.finditer(r"^([A-Z][A-Z0-9_]*)\s*=\s*(.+?)\s*(?:;.*)?$",
                             inc.read_text(), re.M):
            name, val = m.group(1), m.group(2).strip()
            # A name defined in both files with different values would make
            # "the SNES says X" ambiguous, and the check would then be against
            # whichever file was read last.  There are none today.
            if name in seen and seen[name] != val:
                raise SystemExit(
                    f"{name} is defined in more than one include with different "
                    f"values ({seen[name]} and {val}); the specification has to "
                    f"have one answer")
            if name in seen:
                continue
            seen[name] = val
            out.append((name, val))
    return out


def evaluate(expr: str, known: dict[str, int]) -> int | None:
    """One game.inc right-hand side as a number, or None if it is not one.

    `$` is hex and `CELL_X(n)` is the middle of tile n in Q12.4 -- the same
    thing tileCentre() is on the other side, which is what makes comparing them
    meaningful rather than a coincidence of arithmetic.
    """
    e = expr.replace("$", "0x")
    e = re.sub(r"CELL_[XY]\((\d+)\)", lambda m: str(int(m.group(1)) * 256 + 128), e)
    for name, val in known.items():
        e = re.sub(r"\b" + re.escape(name) + r"\b", str(val), e)
    if not re.fullmatch(r"[0-9xXa-fA-F+\-*/() ]+", e):
        return None
    try:
        return int(eval(e, {"__builtins__": {}}, {}))     # noqa: S307
    except Exception:
        return None


def ds_scalars() -> dict[str, int]:
    """Every `constexpr ... NAME = <number>;` the DS headers declare.

    World::fromRaw(n) and tileCentre(n) are the two wrappers that carry a raw
    Q12.4 value, and both are unwrapped to the number underneath, because that
    is the number game.inc holds.
    """
    out: dict[str, int] = {}
    # fixed.h FIRST: constants.h derives from TILE_PX and evaluate() resolves
    # names in the order it meets them.  Then EVERY OTHER HEADER, rather than
    # the hardcoded three this used to read.
    #
    # That list was the second half of the same defect as the missing text.inc.
    # A constant living in any header but those three was invisible here, so the
    # tool would report SC_END as absent while it sat in text.h -- a false
    # negative that makes the check worse than useless, because the honest way
    # to clear it is to add an excuse for something that is not actually
    # missing, and then the excuse goes stale silently.
    #
    # Globbed, so a header added later is read without this file changing.  gen/
    # is deliberately included: a generated constant is still a constant, and
    # gen/assets.h holds several the specification has opinions about.
    first = ("fixed.h", "constants.h", "actor.h")
    rest = sorted(f for f in DS.rglob("*.h")
                  if f.name not in first)
    text = "".join((DS / f).read_text() for f in first)
    text += "".join(f.read_text() for f in rest)
    pat = re.compile(r"constexpr\s+(?:\w+(?:<\d+>)?\s+)+(\w+)\s*=\s*([^;]+);")
    for m in pat.finditer(text):
        name, rhs = m.group(1), m.group(2).strip()
        v = None
        w = re.fullmatch(r"World::fromRaw\(\s*(-?\d+)\s*\)", rhs)
        c = re.fullmatch(r"tileCentre\((\d+)\)", rhs)
        if w:
            v = int(w.group(1))
        elif c:
            v = int(c.group(1)) * 256 + 128
        else:
            v = evaluate(rhs, out)
        if v is not None:
            out[name] = v
    return out


def ds_enum(header: str, name: str) -> dict[str, int]:
    """One `enum class`'s enumerators and their values."""
    text = (DS / header).read_text()
    m = re.search(r"enum class " + name + r"\s*:\s*\w+\s*\{(.*?)\}\s*;",
                  text, re.S)
    if not m:
        return {}
    out: dict[str, int] = {}
    nxt = 0
    for line in m.group(1).split("\n"):
        line = re.sub(r"//.*", "", line)
        for tok in line.split(","):
            tok = tok.strip()
            if not tok:
                continue
            em = re.fullmatch(r"(\w+)(?:\s*=\s*(.+))?", tok)
            if not em:
                continue
            if em.group(2) is not None:
                v = evaluate(em.group(2).strip(), out)
                if v is None:
                    continue
                nxt = v
            out[em.group(1)] = nxt
            nxt += 1
    return out


def ds_namespace(header: str, name: str) -> dict[str, int]:
    """A `namespace x { constexpr uint8_t A = 1, B = 2; }`'s constants."""
    text = (DS / header).read_text()
    m = re.search(r"namespace " + name + r"\s*\{(.*?)\}\s*//\s*namespace " + name,
                  text, re.S)
    if not m:
        return {}
    out: dict[str, int] = {}
    body = re.sub(r"//.*", "", m.group(1))
    for decl in re.finditer(r"constexpr\s+\w+\s+([^;]+);", body):
        for part in decl.group(1).split(","):
            em = re.fullmatch(r"\s*(\w+)\s*=\s*(.+?)\s*", part)
            if not em:
                continue
            v = evaluate(em.group(2), out)
            if v is not None:
                out[em.group(1)] = v
    return out


def camel(tail: str) -> str:
    return "".join(p.capitalize() for p in tail.split("_"))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args(argv)

    consts = snes_constants()
    scalars = ds_scalars()
    enums = {p: ds_enum(h, e) for p, (e, h) in FAMILIES.items()}
    spaces = {p: ds_namespace(h, n) for p, (n, h) in NAMESPACES.items()}
    nm = re.search(r"constexpr int NEED\[[^\]]*\]\s*=\s*\{(.*?)\};",
                   (DS / "constants.h").read_text(), re.S)
    need = [int(x) for x in re.findall(r"^\s*(\d+),", nm.group(1), re.M)] if nm else []

    snes_vals: dict[str, int] = {}
    for name, rhs in consts:
        v = evaluate(rhs, snes_vals)
        if v is not None:
            snes_vals[name] = v

    bad: list[str] = []
    used_excuses: set[str] = set()
    checked = 0
    for name, rhs in consts:
        if name in EXCUSED:
            used_excuses.add(name)
            continue
        want = snes_vals.get(name)
        if name in DERIVED:
            ns, base, offset = DERIVED[name]
            got = spaces["TILE_"].get(base)
            if got is None or got + offset != want:
                bad.append(f"{name}: SNES says {want}, but {ns}::{base} + "
                           f"{offset} is {None if got is None else got + offset}")
            else:
                checked += 1
            continue
        if name in NEED_INDEX:
            got = need[NEED_INDEX[name]] if NEED_INDEX[name] < len(need) else None
            if got != want:
                bad.append(f"{name}: SNES says {want}, "
                           f"NEED[{NEED_INDEX[name]}] says {got}")
            else:
                checked += 1
            continue
        if name in DIVERGED:
            # It is allowed to differ, but it still has to EXIST, and the DS's
            # own name for the SNES value -- where one is kept for the oracle --
            # still has to match.  So the alias is checked and the plain name is
            # not.
            key = ALIASES.get(name)
            if key is None:
                used_excuses.add(name)
                continue
            got = scalars.get(key)
            if got is None:
                bad.append(f"{name}: diverges (divergence {DIVERGED[name]}) but "
                           f"nothing called {key} keeps the SNES value")
            elif got != want:
                bad.append(f"{name}: the SNES value is {want} and {key}, which "
                           f"exists to preserve it, says {got}")
            else:
                checked += 1
            continue
        fam = next((p for p in NAMESPACES if name.startswith(p)), None)
        if fam and name not in NOT_A_FAMILY:
            ns, _ = NAMESPACES[fam]
            key = RENAMED.get(name, camel(name[len(fam):]))
            got = spaces[fam].get(key)
            where = f"{ns}::{key}"
            if got is None and want is not None:
                bad.append(f"{name} = {want}: nothing called {where}")
                continue
            if got != want:
                bad.append(f"{name}: SNES says {want}, {where} says {got}")
                continue
            checked += 1
            if args.verbose:
                print(f"  ok  {name:18} = {want:<8} {where}")
            continue
        fam = next((p for p in FAMILIES if name.startswith(p)), None)
        if name in NOT_A_FAMILY:
            fam = None
        if fam:
            enum, header = FAMILIES[fam]
            key = RENAMED.get(name, camel(name[len(fam):]))
            got = enums[fam].get(key)
            where = f"{enum}::{key}"
        else:
            key = ALIASES.get(name, name)
            got = scalars.get(key)
            where = key
        if want is None:
            bad.append(f"{name}: cannot evaluate {rhs!r} on the SNES side")
            continue
        if got is None:
            bad.append(f"{name} = {want}: nothing called {where} in the DS "
                       f"headers.  Port it, or excuse it with a reason.")
            continue
        if got != want:
            bad.append(f"{name}: SNES says {want}, {where} says {got}")
            continue
        checked += 1
        if args.verbose:
            print(f"  ok  {name:18} = {want:<8} {where}")

    stale = sorted(set(EXCUSED) - used_excuses)
    for n in stale:
        bad.append(f"{n} is excused and no longer exists in either include; a "
                   f"stale exclusion is how a checker stops checking")

    if bad:
        print(f"constants: {len(bad)} problem(s)\n", file=sys.stderr)
        for b in bad:
            print(f"  {b}", file=sys.stderr)
        return 1
    print(f"constants ok: {checked} of "
          f"{' and '.join(i.name for i in INCS)} checked by value against the "
          f"DS headers, {len(EXCUSED)} excused with a reason")
    return 0


if __name__ == "__main__":
    sys.exit(main())
