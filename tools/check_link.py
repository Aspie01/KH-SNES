#!/usr/bin/env python3
"""Will the device build LINK?  Answer the part of that question that is checkable here.

    tools/check_link.py

THE PROBLEM THIS EXISTS FOR.  A DS cartridge has no filesystem.  Every table the
game reads is a symbol linked into the binary by bin2s, and
platform/ds/arm9/source/main.cpp declares each one by hand:

    KH_BIN(station3boss);

A name that does not match a file in assets/gen/ds/ is an undefined reference at
link time -- which is a perfectly good error, and which nobody in this container
can see, because devkitPro is not installed here (tools/check_device.py).  So a
typo in that list would sit in the tree looking correct until somebody with a
toolchain tried to build it, and the first thing they would learn about this
port is that it does not link.

The other direction matters too and is quieter.  platform/ds/arm9/Makefile
points DATA at the whole of assets/gen/ds, so EVERY .bin is converted and
linked whether or not anything references it.  A table nobody declares is dead
weight in the cartridge, and -- worse -- is indistinguishable from a table
somebody forgot to wire up.  The island's day-two provisions were exactly that
shape of omission for six milestones.

AND THE THIRD CHECK IS THE ONE WITH TEETH: gen/assets.h's SCENE_ASSETS declares
which optional tables each scene has, and the generator is the only thing that
knows.  A scene whose SceneTable says Spots but whose <scene>spots.bin is absent
is a night with nowhere for the Heartless to come up, and the symptom is an
empty search rather than an error.

None of this needs a compiler.  It is three set comparisons over the tree.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets" / "gen" / "ds"
MAIN = ROOT / "platform" / "ds" / "arm9" / "source" / "main.cpp"
ASSETS_H = ROOT / "platform" / "ds" / "include" / "gen" / "assets.h"

# The optional-table suffixes, and which SceneTable flag turns each on.  The
# names are gen/assets.h's enum and the suffixes are what build_assets.py emits;
# they are paired here because that pairing exists nowhere else in one place.
OPTIONAL = {
    "Spots": "spots",
    "Doors": "doors",
    "Boss": "boss",
    "Pair": "pair",
    "Day1": "day1",
    "Day2": "day2",
}


def _uses(macro: str) -> set[str]:
    """Every `<name>` in `macro(<name>)`, skipping the macro's own definition.

    The `#define KH_BIN(name)` line is itself a match, and the first version of
    this tool duly reported that `name.bin` was missing -- which is funny once
    and would be noise for ever.
    """
    names = set()
    for line in MAIN.read_text().splitlines():
        if line.lstrip().startswith("#define"):
            continue
        names.update(re.findall(rf"\b{macro}\(\s*([A-Za-z0-9_]+)\s*\)", line))
    return names


def declared_bins() -> set[str]:
    """The `<name>` of every KH_BIN(<name>) in the ARM9's main."""
    return _uses("KH_BIN")


def referenced_bins() -> set[str]:
    return _uses("KH_BLOB")


def present_bins() -> set[str]:
    return {p.stem for p in ASSETS.glob("*.bin")}


def scene_rows() -> list[tuple[str, str | None, set[str]]]:
    """(name, groundFrom, {optional table flags}) for each SCENE_ASSETS row.

    Parsed out of the generated header rather than re-derived from the .txt
    sources: the header is what the DEVICE compiles against, so it is the thing
    whose claims have to match the files.
    """
    text = ASSETS_H.read_text()
    block = re.search(r"constexpr SceneAsset SCENE_ASSETS\[\] = \{(.*?)\n\};",
                      text, re.S)
    if not block:
        raise SystemExit("gen/assets.h has no SCENE_ASSETS table; regenerate it")
    rows = []
    for line in block.group(1).splitlines():
        line = line.strip()
        if not line.startswith("{"):
            continue
        name = re.match(r'\{"([^"]+)"', line)
        if not name:
            continue
        # {"night", 64, 32, 0, "island", true, -1, ...} -- the name, three
        # numbers, then groundFrom.  Anchored on the numbers rather than on the
        # position of the quotes, because a row that borrows and one that does
        # not have the same shape only up to here.
        ground = re.match(r'\{"[^"]+",\s*\d+,\s*\d+,\s*\d+,\s*'
                          r'(?:"([^"]+)"|nullptr)', line)
        borrows = ground.group(1) if ground and ground.group(1) else None
        flags = set(re.findall(r"SceneTable::(\w+)", line)) - {"None"}
        rows.append((name.group(1), borrows, flags))
    return rows


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="list every name checked, not only the problems")
    args = ap.parse_args(argv)

    if not ASSETS.is_dir():
        print("assets/gen/ds does not exist; run tools/build_assets.py first")
        return 1

    present = present_bins()
    declared = declared_bins()
    referenced = referenced_bins()
    problems: list[str] = []

    # 1. Every declared symbol has a file behind it.  This is the undefined
    #    reference, found before a toolchain exists to find it.
    for name in sorted(declared - present):
        problems.append(
            f"main.cpp declares KH_BIN({name}) and assets/gen/ds/{name}.bin does "
            f"not exist -- an undefined reference to {name}_bin at link time")

    # 2. Every symbol used is declared.  A compiler would catch this too; it is
    #    free here and it names the line rather than a macro expansion.
    for name in sorted(referenced - declared):
        problems.append(f"main.cpp uses KH_BLOB({name}) with no KH_BIN({name})")

    # 3. Every scene's required and declared-optional tables are on disk.
    for name, borrows, flags in scene_rows():
        ground = borrows or name
        for suffix, why in (("coll", "the collision map"),
                            ("height", "the height map"),
                            ("map", "the character map")):
            if f"{ground}{suffix}" not in present:
                problems.append(f"{name}: {ground}{suffix}.bin is missing "
                                f"({why})")
        if f"{name}cast" not in present:
            problems.append(f"{name}: {name}cast.bin is missing (the [base] cast)")
        for flag, suffix in OPTIONAL.items():
            if flag in flags and f"{name}{suffix}" not in present:
                problems.append(
                    f"{name}: SCENE_ASSETS says it has a {flag} table and "
                    f"{name}{suffix}.bin is not there -- the generator and its "
                    f"own output disagree")
            if flag not in flags and f"{name}{suffix}" in present:
                problems.append(
                    f"{name}: {name}{suffix}.bin exists and SCENE_ASSETS does "
                    f"not declare a {flag} table, so nothing on the device can "
                    f"know it is there")

    # 4. Linked and unreferenced.  Not fatal -- it is ROM, not a fault -- but it
    #    is reported by name, because "in the cartridge and read by nothing" is
    #    what an unwired table looks like from the outside.
    unused = sorted(present - declared)

    if args.verbose:
        for name in sorted(declared):
            print(f"  ok   {name}.bin")

    for p in problems:
        print(f"  NO   {p}")
    if unused:
        print(f"\n{len(unused)} .bin file(s) are linked into the cartridge and "
              f"declared by nothing in main.cpp:")
        for name in unused:
            print(f"       {name}.bin")
        print("  Each is either dead weight or a table somebody forgot to wire "
              "up, and this tool cannot tell which.")

    if problems:
        print(f"\n{len(problems)} problem(s): the device build would not link, "
              f"or would link against a scene whose data is not there.")
        return 1
    print(f"{len(declared)} linked asset(s) declared, all present; "
          f"{len(scene_rows())} scene(s), every table the generator declares is "
          f"on disk.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
