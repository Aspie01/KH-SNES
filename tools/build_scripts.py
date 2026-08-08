#!/usr/bin/env python3
"""Extract every dialogue script from the assembly and emit them as C++.

Seventy-odd scripts, five thousand characters.  Retyping that into a header is
the single most error-prone job in the port and the one where a mistake is least
likely to be noticed -- a dropped byte does not crash, it just makes somebody say
something slightly wrong on a screen nobody is looking at.

So it is not retyped.  It is parsed out of the .byte runs AND CHECKED BYTE FOR
BYTE AGAINST THE FROZEN ROM: ca65's debug file gives every script label's
absolute address, LoROM maps that to a file offset, and the bytes there are what
the assembler actually produced.  If the parse and the ROM disagree, this refuses
to emit anything.  That check is worth more than any amount of proof-reading,
because it compares against the artefact rather than against the source.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SNES = ROOT / "platform" / "snes"
OUT = ROOT / "platform" / "ds" / "include" / "gen" / "scripts.h"

# Which files carry dialogue, and the prefix each one's scripts get in the
# generated enum.  Two scenes both have a script called "Card"; namespacing by
# scene is what keeps them apart, and it reads better at the call site anyway.
SOURCES = (
    ("dive.s", "Dive"),
    ("island.s", "Island"),
    ("night.s", "Night"),
    ("town.s", "Town"),
)

# text.inc: anything below 32 is a code, not a character.
CODES = {"SC_END": 0x00, "SC_NL": 0x01, "SC_PAGE": 0x02}

LABEL = re.compile(r"^(script\w+):\s*$")
BYTE = re.compile(r"^\s*\.byte\s+(.*?)\s*(?:;.*)?$")
# A .word table of script pointers -- the lines Kairi picks between, and so on.
TABLE = re.compile(r"^(\w+Lines):\s*$")
WORD = re.compile(r"^\s*\.word\s+(.*?)\s*(?:;.*)?$")


def parse_operands(text: str, where: str) -> list[int]:
    """One .byte operand list into bytes.  Refuses anything it does not know."""
    out: list[int] = []
    i = 0
    while i < len(text):
        c = text[i]
        if c in ", \t":
            i += 1
            continue
        if c == '"':
            j = text.index('"', i + 1)
            for ch in text[i + 1:j]:
                if ord(ch) < 32 or ord(ch) > 126:
                    raise SystemExit(f"{where}: {ch!r} is not a printable byte")
                out.append(ord(ch))
            i = j + 1
            continue
        m = re.match(r"[A-Za-z_]\w*", text[i:])
        if not m or m.group(0) not in CODES:
            raise SystemExit(f"{where}: cannot read operand at {text[i:][:24]!r}")
        out.append(CODES[m.group(0)])
        i += len(m.group(0))
    return out


def scripts_in(path: Path, prefix: str):
    """Every script label in one file, in source order, with its bytes."""
    lines = path.read_text().splitlines()
    found = []
    for n, line in enumerate(lines):
        m = LABEL.match(line)
        if not m:
            continue
        name = m.group(1)
        data: list[int] = []
        k = n + 1
        while k < len(lines):
            b = BYTE.match(lines[k])
            if not b:
                break
            data += parse_operands(b.group(1), f"{path.name}:{k + 1}")
            k += 1
        if not data:
            raise SystemExit(f"{path.name}:{n + 1}: {name} has no bytes")
        if data[-1] != CODES["SC_END"]:
            raise SystemExit(f"{path.name}:{n + 1}: {name} does not end in SC_END; "
                             f"RevealAll would run to its 512-iteration backstop")
        found.append((path.name, name, prefix + enum_name(name, prefix),
                      data, n + 1))
    return found


def enum_name(label: str, prefix: str) -> str:
    """scriptKairiAsk2 -> KairiAsk2, with the scene prefix added by the caller."""
    stem = label[len("script"):]
    return stem[0].upper() + stem[1:]


def tables_in(path: Path):
    """The .word tables that index scripts, e.g. kairiLines."""
    lines = path.read_text().splitlines()
    out = {}
    for n, line in enumerate(lines):
        m = TABLE.match(line)
        if not m:
            continue
        entries: list[str] = []
        k = n + 1
        while k < len(lines):
            w = WORD.match(lines[k])
            if not w:
                break
            for tok in w.group(1).split(","):
                tok = tok.strip()
                s = re.search(r"script\w+", tok)
                if not s:
                    raise SystemExit(f"{path.name}:{k + 1}: {tok!r} is not a script")
                entries.append(s.group(0))
            k += 1
        if entries:
            out[m.group(1)] = entries
    return out


def dbg_addresses() -> dict[tuple[str, str], int]:
    """Every script label's absolute address, keyed by (source file, label).

    Keyed by BOTH because the labels are not unique: island.s and night.s each
    have a scriptRiku, and they are different lines said by different people at
    different points in the story.  ca65 scopes them per module, so the debug
    file's scope -> mod -> name chain is what tells them apart -- and getting
    this wrong would have verified one of them against the other's bytes and
    reported success.
    """
    dbg = SNES / "build" / "kh.dbg"
    if not dbg.exists():
        raise SystemExit(f"{dbg} is missing -- run `make -C platform/snes` first.\n"
                         "The ROM check is not optional: without it this tool is "
                         "just a transcription with no second opinion.")
    text = dbg.read_text()
    mods = {int(m.group(1)): m.group(2)
            for m in re.finditer(r'^mod\s+id=(\d+),name="([^"]+)"', text, re.M)}
    scopes = {int(m.group(1)): int(m.group(2))
              for m in re.finditer(r'^scope\s+id=(\d+),[^\n]*?\bmod=(\d+)',
                                   text, re.M)}
    out: dict[tuple[str, str], int] = {}
    for m in re.finditer(
            r'name="(script\w+)",[^\n]*?scope=(\d+),[^\n]*?val=0x([0-9A-Fa-f]+)',
            text):
        name, scope, val = m.group(1), int(m.group(2)), int(m.group(3), 16)
        mod = mods.get(scopes.get(scope, -1), "")
        if not mod.endswith(".o"):
            raise SystemExit(f"{name}: scope {scope} does not resolve to a module")
        key = (mod[:-2] + ".s", name)
        if key in out and out[key] != val:
            raise SystemExit(f"{key}: two different addresses in kh.dbg")
        out[key] = val
    return out


def lorom_offset(addr: int) -> int:
    """A 24-bit LoROM address to an offset into the ROM file."""
    bank = (addr >> 16) & 0xFF
    off = addr & 0xFFFF
    if off < 0x8000:
        raise SystemExit(f"${addr:06X} is not in a LoROM ROM window")
    return ((bank & 0x7F) * 0x8000) + (off - 0x8000)


def verify(all_scripts, addrs) -> None:
    """The whole point of this file: compare every script against the ROM."""
    rom = (SNES / "kh.sfc").read_bytes()
    checked = 0
    for src, name, _enum, data, line in all_scripts:
        if (src, name) not in addrs:
            raise SystemExit(f"{src}:{name} has no address in kh.dbg; the ROM "
                             f"check cannot be skipped for one script")
        got = rom[lorom_offset(addrs[(src, name)]):][:len(data)]
        if list(got) != data:
            for i, (a, b) in enumerate(zip(data, got)):
                if a != b:
                    raise SystemExit(
                        f"{src}:{line} {name} differs from the ROM at byte {i}: "
                        f"parsed {a:#04x} ({chr(a) if a >= 32 else '.'!r}), "
                        f"ROM has {b:#04x} ({chr(b) if b >= 32 else '.'!r})")
            raise SystemExit(f"{name}: parsed {len(data)} bytes, ROM has fewer")
        checked += 1
    print(f"scripts:  {checked} verified byte-for-byte against kh.sfc")


def char_lit(b: int) -> str:
    """One printable byte as a C++ character literal."""
    ch = chr(b)
    if ch == "'":
        return r"'\''"
    if ch == "\\":
        return r"'\\'"
    return f"'{ch}'"


def rows(data: list[int]):
    """Split a script into emitted lines: one per run of text, one per code run.

    A string literal cannot initialise a uint8_t[], so the characters go in as
    character literals -- exact, and no cast anywhere.  The readable text goes in
    a trailing comment, which is what makes a diff of this file mean something.
    """
    out = []
    run: list[int] = []

    def flush():
        if run:
            out.append((list(run), "".join(chr(c) for c in run)))
            run.clear()

    codes: list[int] = []

    def flush_codes():
        if codes:
            out.append((list(codes), None))
            codes.clear()

    for b in data:
        if b >= 32:
            flush_codes()
            run.append(b)
        else:
            flush()
            codes.append(b)
    flush()
    flush_codes()
    return out


NAME_OF_CODE = {0: "SC_END", 1: "SC_NL", 2: "SC_PAGE"}


def emit(all_scripts, tables) -> str:
    lines = []
    w = lines.append
    w("#pragma once")
    w("// GENERATED by tools/build_scripts.py -- do not edit.")
    w("//")
    w("// Every dialogue script in the game, parsed out of the .byte runs in")
    w("// platform/snes/src/*.s and CHECKED BYTE FOR BYTE against the frozen ROM,")
    w("// using ca65's debug file for the addresses.  Regenerate with:")
    w("//")
    w("//     make -C platform/snes && python3 tools/build_scripts.py")
    w("//")
    w("// A host test re-runs the extraction and fails if this file has drifted, so")
    w("// it can be trusted the way the emitted asset binaries are.")
    w("//")
    w("// The format is unchanged: bytes >= 32 are characters, below are control")
    w("// codes.  SC_PAGE PAGES HERE AND DID NOT ON THE SNES -- see")
    w("// docs/behaviour/divergences/005-sc-page.md.  Nearly two thirds of what is")
    w("// below was unreachable on the original hardware.")
    w("")
    w("#include <cstddef>")
    w("#include <cstdint>")
    w("")
    w('#include "text.h"')
    w("")
    w("namespace kh {")
    w("")
    w("enum class ScriptId : uint16_t {")
    w("    None = 0,")
    scene = None
    for src, name, enum, data, line in all_scripts:
        pre = ("Dive" if enum.startswith("Dive") else
               "Island" if enum.startswith("Island") else
               "Night" if enum.startswith("Night") else "Town")
        if pre != scene:
            scene = pre
            w(f"    // {scene}")
        w(f"    {enum},")
    w("    Count,")
    w("};")
    w("")
    w("namespace script {")
    w("")
    for src, name, enum, data, line in all_scripts:
        w(f"// {name}, {src}:{line} -- {len(data)} bytes")
        w(f"constexpr uint8_t {enum}[] = {{")
        for bytes_, text in rows(data):
            if text is None:
                w("    " + " ".join(NAME_OF_CODE[b] + "," for b in bytes_))
            else:
                cells = "".join(char_lit(b) + "," for b in bytes_)
                w(f"    {cells}".ljust(78) + f"// {text}")
        w("};")
        w("")
    w("}  // namespace script")
    w("")
    w("// Indexed by ScriptId.  None is a null Script, and so is anything out of")
    w("// range: Dialogue::open treats that as an empty message rather than reading")
    w("// off the end of something.")
    w("constexpr Script SCRIPTS[] = {")
    w("    Script{nullptr, 0},")
    for src, name, enum, data, line in all_scripts:
        w(f"    Script{{script::{enum}, sizeof script::{enum}}},")
    w("};")
    w("static_assert(sizeof SCRIPTS / sizeof SCRIPTS[0]")
    w("              == static_cast<size_t>(ScriptId::Count),")
    w("              \"every ScriptId needs a row, and every row an id\");")
    w("")
    w("constexpr Script scriptFor(ScriptId id) {")
    w("    return static_cast<size_t>(id) < static_cast<size_t>(ScriptId::Count)")
    w("               ? SCRIPTS[static_cast<size_t>(id)]")
    w("               : Script{nullptr, 0};")
    w("}")
    w("")
    for tname, entries in sorted(tables.items()):
        w(f"// {tname}: the lines picked between by index.")
        w(f"constexpr ScriptId {tname.upper()}[] = {{")
        for key in entries:
            w(f"    ScriptId::{ENUM_OF[key]},")
        w("};")
        w("")
    w("}  // namespace kh")
    return "\n".join(lines) + "\n"


ENUM_OF: dict[tuple[str, str], str] = {}


def main() -> int:
    all_scripts = []
    tables: dict[str, list[str]] = {}
    for fname, prefix in SOURCES:
        path = SNES / "src" / fname
        found = scripts_in(path, prefix)
        for src, name, enum, data, line in found:
            if enum in ENUM_OF.values():
                raise SystemExit(f"{enum} would be emitted twice")
            ENUM_OF[(src, name)] = enum
        all_scripts += found
        for tname, entries in tables_in(path).items():
            if tname in tables:
                raise SystemExit(f"{tname} is defined in two files")
            tables[tname] = [(fname, e) for e in entries]

    verify(all_scripts, dbg_addresses())

    total = sum(len(d) for _, _, _, d, _ in all_scripts)
    pages = sum(d.count(CODES["SC_PAGE"]) for _, _, _, d, _ in all_scripts)
    paged = sum(1 for _, _, _, d, _ in all_scripts if CODES["SC_PAGE"] in d)
    print(f"          {len(all_scripts)} scripts, {total} bytes, "
          f"{pages} page breaks in {paged} of them")
    for tname, entries in sorted(tables.items()):
        print(f"          {tname}: {len(entries)} entries")

    OUT.parent.mkdir(parents=True, exist_ok=True)
    text = emit(all_scripts, tables)
    same = OUT.exists() and OUT.read_text() == text

    # --check makes this a gate rather than a generator: the header is COMMITTED,
    # so that a clone can build the DS tier without running Python or the SNES
    # assembler, and a committed generated file that nothing re-checks is a
    # committed generated file that rots.
    if "--check" in sys.argv:
        if same:
            print(f"          {OUT.relative_to(ROOT)} is up to date")
            return 0
        print(f"{OUT.relative_to(ROOT)} has drifted from the assembly.\n"
              f"Run: make -C platform/snes && python3 tools/build_scripts.py",
              file=sys.stderr)
        return 1

    if same:
        print(f"          {OUT.relative_to(ROOT)} unchanged")
    else:
        OUT.write_text(text)
        print(f"          wrote {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
