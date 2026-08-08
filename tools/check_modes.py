#!/usr/bin/env python3
"""Find places where the assembler's idea of the register widths is wrong.

On the 65816 the width of an immediate operand is not in the opcode -- `LDA #`
is two bytes with an eight-bit accumulator and three with a sixteen-bit one,
and the assembler only knows which from the `.a8` / `.a16` directives it has
been given.  Those directives are sequential, so this happens easily:

        rep #$20
        .a16
        lda #SOMETHING
        bra @apply
    @west:                  <-- reached in EIGHT-bit mode...
        ora #AF_HFLIP       <-- ...but assembled three bytes wide

and the extra `00` the assembler emits is a BRK the CPU walks straight into.
Everything after that is misaligned instruction bytes, so the damage lands
wherever those bytes happen to address -- which makes it look like anything
except what it is.  This has cost four separate debugging sessions.

The check, for every branch: take the widths the CPU will actually arrive at
the target with -- the ones in force at the branch -- and walk forward from
the label alongside the assembler's own assumption, stepping the CPU's widths
through any `rep` / `sep` on the way.  Complain the first time an immediate is
sized against an assumption the CPU does not share.

A target that opens with `sep #$20` is doing exactly the right thing even
though the directive under it disagrees with the branch, so the walk stops as
soon as the two agree again.  That distinction is the whole value of the tool:
the naive version of this check reports thirty-odd sites in this codebase and
all but one of them are correct code.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

BRANCHES = {"bra", "beq", "bne", "bcc", "bcs", "bmi", "bpl", "bvc", "bvs",
            "brl", "jmp", "jml"}

# Instructions whose immediate operand is one byte or two depending on a
# register width.  Everything else is the same size either way.
A_IMM = {"adc", "and", "bit", "cmp", "eor", "lda", "ora", "sbc"}
I_IMM = {"cpx", "cpy", "ldx", "ldy"}
# Nothing after one of these falls through, so the walk stops.
TERMINAL = {"rts", "rtl", "rti", "bra", "brl", "jmp", "jml"}

LABEL_RE = re.compile(r"^([A-Za-z_@][A-Za-z0-9_]*):")
ANON_RE = re.compile(r"^:(?![+-])")
INSTR_RE = re.compile(r"^\s*([a-z]{3})\b\s*(.*)$")
ANON_TARGET_RE = re.compile(r"^:([+-]+)$")


class Site:
    __slots__ = ("line", "a", "i", "proc")

    def __init__(self, line: int, a: int, i: int, proc: str):
        self.line, self.a, self.i, self.proc = line, a, i, proc


def strip(line: str) -> str:
    out, quote = [], None
    for ch in line:
        if quote:
            out.append(ch)
            if ch == quote:
                quote = None
        elif ch in "\"'":
            quote = ch
            out.append(ch)
        elif ch == ";":
            break
        else:
            out.append(ch)
    return "".join(out).rstrip()


def scan(path: Path) -> list[str]:
    """Return a list of complaints for one source file."""
    lines = [strip(ln) for ln in path.read_text().splitlines()]

    a, i = 8, 16                    # ca65's defaults for .p816 sources
    proc = ""
    in_macro = False

    labels: dict[tuple[str, str], Site] = {}    # (proc, name) -> where
    anon: list[Site] = []                       # anonymous ":" labels in order
    branches: list[tuple[int, int, int, str, str]] = []   # line, a, i, proc, target

    # Pass one: walk the file the way the assembler does.
    for n, raw in enumerate(lines, 1):
        text = raw.strip()
        if not text:
            continue
        low = text.lower()

        if low.startswith(".macro"):
            in_macro = True
            continue
        if low.startswith(".endmacro"):
            in_macro = False
            continue
        if in_macro:
            continue

        if low.startswith(".proc"):
            proc = text.split()[1] if len(text.split()) > 1 else ""
            labels[("", proc)] = Site(n, a, i, "")
            continue
        if low.startswith(".endproc"):
            proc = ""
            continue

        if low.startswith(".a8"):
            a = 8
            continue
        if low.startswith(".a16"):
            a = 16
            continue
        if low.startswith(".i8"):
            i = 8
            continue
        if low.startswith(".i16"):
            i = 16
            continue

        # A label may carry code on the same line, so record it and go on.
        m = LABEL_RE.match(text)
        if m:
            labels[(proc if m.group(1).startswith("@") else "", m.group(1))] = \
                Site(n, a, i, proc)
            text = text[m.end():].strip()
        elif ANON_RE.match(text):
            anon.append(Site(n, a, i, proc))
            text = text[1:].strip()

        if not text:
            continue

        m = INSTR_RE.match(text)
        if m and m.group(1) in BRANCHES:
            branches.append((n, a, i, proc, m.group(2).strip()))

    def verdict(site: Site, cpu_a: int, cpu_i: int) -> tuple[int, str] | None:
        """Walk forward from a label with the widths the CPU actually arrives
        with, and see whether the assembler ever sizes an immediate wrongly.

        A target that opens with a `sep #$20` is doing exactly the right thing
        even though the directive after it disagrees with the branch, so the
        walk stops as soon as the two agree again.
        """
        asm_a, asm_i = site.a, site.i
        for off, raw in enumerate(lines[site.line - 1:], site.line - 1):
            text = raw.strip()
            if not text:
                continue
            low = text.lower()
            if low.startswith(".a8"):
                asm_a = 8
                continue
            if low.startswith(".a16"):
                asm_a = 16
                continue
            if low.startswith(".i8"):
                asm_i = 8
                continue
            if low.startswith(".i16"):
                asm_i = 16
                continue
            if low.startswith("."):
                continue
            m = LABEL_RE.match(text) or ANON_RE.match(text)
            if m:
                text = text[m.end():].strip()
                if not text:
                    continue
            m = INSTR_RE.match(text)
            if not m:
                continue
            op, arg = m.group(1), m.group(2).strip()

            if op in ("rep", "sep") and arg.startswith("#"):
                try:
                    bits = int(arg[1:].replace("$", "0x"), 0)
                except ValueError:
                    return None
                wide = op == "rep"
                if bits & 0x20:
                    cpu_a = 16 if wide else 8
                if bits & 0x10:
                    cpu_i = 16 if wide else 8
                if cpu_a == asm_a and cpu_i == asm_i:
                    return None                 # reconciled: nothing to report
                continue

            if arg.startswith("#"):
                if op in A_IMM and cpu_a != asm_a:
                    return off + 1, f"{op} # is assembled {asm_a}-bit, CPU is {cpu_a}-bit"
                if op in I_IMM and cpu_i != asm_i:
                    return off + 1, f"{op} # is assembled {asm_i}-bit, CPU is {cpu_i}-bit"

            if cpu_a == asm_a and cpu_i == asm_i:
                return None
            if op in TERMINAL:
                return None
        return None

    bad = []
    for line, ba, bi, bproc, target in branches:
        m = ANON_TARGET_RE.match(target)
        if m:
            signs = m.group(1)
            if signs[0] == "+":
                ahead = [s for s in anon if s.line > line]
                if len(ahead) < len(signs):
                    continue
                site = ahead[len(signs) - 1]
            else:
                back = [s for s in anon if s.line < line]
                if len(back) < len(signs):
                    continue
                site = back[-len(signs)]
        else:
            name = target.split()[0] if target else ""
            site = labels.get((bproc, name)) or labels.get(("", name))
            if site is None:
                continue                    # imported, or a macro's own label

        hit = verdict(site, ba, bi)
        if hit:
            at, why = hit
            where = f"{bproc}:" if bproc else ""
            bad.append(
                f"  {path.name}:{at}: reached by the branch at line {line} "
                f"(to {where}{target}) with A{ba}/I{bi} -- {why}")
    return sorted(set(bad))


def main(argv: list[str]) -> int:
    paths = [Path(p) for p in argv[1:]] or sorted((ROOT / "src").glob("*.s"))
    bad: list[str] = []
    for p in paths:
        bad += scan(p)
    if bad:
        print("register width disagreements between a branch and its target:")
        print("\n".join(bad))
        return 1
    print(f"modes ok: {len(paths)} sources, every branch target assembled at "
          f"the width the CPU arrives with")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
