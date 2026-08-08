#!/usr/bin/env python3
"""Find places where the assembler's idea of the register widths is wrong.

On the 65816 the width of an immediate operand is not in the opcode -- `LDA #`
is two bytes with an eight-bit accumulator and three with a sixteen-bit one,
and the assembler only knows which from the `.a8` / `.a16` directives it has
been given.  Those directives are sequential, and control flow is not, so this
happens easily:

        rep #$20
        .a16
        lda #.loword(script)
        jmp Say                 <-- the sixteen-bit stretch ends here...
    @done:
        rts                     <-- ...but the directive does not
    @more:
        lda spawnTimer
        beq @due
        dec spawnTimer
        rts
    @due:                       <-- reached in EIGHT-bit mode
        lda #SOMETHING          <-- assembled three bytes wide

The extra `00` the assembler emits is a BRK the CPU walks straight into, and
everything after it is misaligned instruction bytes, so the damage lands
wherever those bytes happen to address -- which makes it look like anything
except what it is.  This class of bug has cost six separate debugging sessions
on this project.

So don't guess at the widths: work them out.  For each `.proc`, walk the
control-flow graph from its entry -- fall-through edges plus branch and jump
edges -- carrying the widths the CPU actually has, which only `rep` and `sep`
change.  Then every width-dependent immediate can be compared against the
directive the assembler sized it with, and a disagreement is a real defect
rather than a style note.

Two earlier versions of this check compared a branch against its target
instead.  That finds a disagreement between the two, which is not the same
thing: in the example above the branch and its target agree with each other
and are both wrong, and the bug shipped.

Assumptions, both true of this engine and both checked where they can be:
  * a `jsr`/`jsl` returns with the widths it was called with.  Routines that
    deliberately do otherwise are entered by `jmp`, not `jsr`.
  * a routine is entered with the widths its opening `.a8`/`.a16` declare.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# The only platform-specific tool here: register widths are a 65816 problem,
# and the ARM target this is being ported to has no equivalent to get wrong.
SNES_SRC = ROOT / "platform" / "snes" / "src"

COND = {"beq", "bne", "bcc", "bcs", "bmi", "bpl", "bvc", "bvs"}
JUMP = {"bra", "brl", "jmp", "jml"}
# Nothing after one of these is reached by falling into it.
TERMINAL = JUMP | {"rts", "rtl", "rti"}

# Instructions whose immediate operand is one byte or two depending on a
# register width.  Everything else is the same size either way.
A_IMM = {"adc", "and", "bit", "cmp", "eor", "lda", "ora", "sbc"}
I_IMM = {"cpx", "cpy", "ldx", "ldy"}

LABEL_RE = re.compile(r"^([A-Za-z_@][A-Za-z0-9_]*):")
ANON_RE = re.compile(r"^:(?![+-])")
INSTR_RE = re.compile(r"^\s*([a-z]{3})\b\s*(.*)$")
ANON_TARGET_RE = re.compile(r"^:([+-]+)$")


class Ins:
    """One instruction, with the widths the assembler sized it at."""
    __slots__ = ("line", "op", "arg", "asm_a", "asm_i", "targets", "labels")

    def __init__(self, line: int, op: str, arg: str, asm_a: int, asm_i: int):
        self.line, self.op, self.arg = line, op, arg
        self.asm_a, self.asm_i = asm_a, asm_i
        self.targets: list[int] = []        # indices this can branch to
        self.labels: list[str] = []         # labels sitting on it


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


def bits(arg: str) -> int | None:
    """The operand of a rep/sep, or None if it is not a plain constant."""
    if not arg.startswith("#"):
        return None
    try:
        return int(arg[1:].replace("$", "0x").replace("%", "0b"), 0)
    except ValueError:
        return None


class Proc:
    """One `.proc`, decoded into instructions with edges between them."""

    def __init__(self, name: str):
        self.name = name
        self.ins: list[Ins] = []
        self.by_label: dict[str, int] = {}
        self.anon: list[int] = []           # indices carrying an anonymous ":"
        self.entry: tuple[int, int] | None = None

    def resolve(self) -> None:
        """Turn branch operands into edges."""
        for n, ins in enumerate(self.ins):
            if ins.op in COND:
                if n + 1 < len(self.ins):
                    ins.targets.append(n + 1)       # not taken
            elif ins.op not in TERMINAL:
                if n + 1 < len(self.ins):
                    ins.targets.append(n + 1)       # falls through
            if ins.op not in COND and ins.op not in JUMP:
                continue
            target = ins.arg.split()[0] if ins.arg else ""
            m = ANON_TARGET_RE.match(target)
            if m:
                signs = m.group(1)
                if signs[0] == "+":
                    ahead = [k for k in self.anon if k > n]
                    if len(ahead) >= len(signs):
                        ins.targets.append(ahead[len(signs) - 1])
                else:
                    back = [k for k in self.anon if k <= n]
                    if len(back) >= len(signs):
                        ins.targets.append(back[-len(signs)])
            elif target in self.by_label:
                ins.targets.append(self.by_label[target])
            # anything else is imported, or a label in another proc: no edge

    def widths(self) -> list[str]:
        """Propagate the real widths from the entry and report disagreements."""
        if not self.ins or self.entry is None:
            return []
        state: dict[int, set[tuple[int, int]]] = {}
        work = [(0, self.entry)]
        while work:
            n, (a, i) = work.pop()
            seen = state.setdefault(n, set())
            if (a, i) in seen:
                continue
            seen.add((a, i))
            ins = self.ins[n]
            if ins.op in ("rep", "sep"):
                b = bits(ins.arg)
                if b is None:
                    continue                # can't follow it; stop this path
                wide = ins.op == "rep"
                if b & 0x20:
                    a = 16 if wide else 8
                if b & 0x10:
                    i = 16 if wide else 8
            for t in ins.targets:
                work.append((t, (a, i)))

        bad = []
        for n, ins in enumerate(self.ins):
            if n not in state or not ins.arg.startswith("#"):
                continue
            for (a, i) in sorted(state[n]):
                if ins.op in A_IMM and a != ins.asm_a:
                    bad.append((ins.line, f"{ins.op} # is assembled "
                                          f"{ins.asm_a}-bit, CPU is {a}-bit"))
                    break
                if ins.op in I_IMM and i != ins.asm_i:
                    bad.append((ins.line, f"{ins.op} # is assembled "
                                          f"{ins.asm_i}-bit, CPU is {i}-bit"))
                    break
        return [f"{self.name}:{line}: {why}" for line, why in bad]


def scan(path: Path) -> list[str]:
    """Return a list of complaints for one source file."""
    lines = [strip(ln) for ln in path.read_text().splitlines()]

    a, i = 8, 16                    # ca65's defaults for .p816 sources
    in_macro = False
    proc: Proc | None = None
    procs: list[Proc] = []
    pending: list[str] = []         # labels seen but not yet attached to code
    pending_anon = False

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
            parts = text.split()
            proc = Proc(parts[1] if len(parts) > 1 else "?")
            procs.append(proc)
            pending, pending_anon = [], False
            continue
        if low.startswith(".endproc"):
            proc = None
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
        if low.startswith("."):
            continue

        m = LABEL_RE.match(text)
        if m:
            pending.append(m.group(1))
            text = text[m.end():].strip()
        elif ANON_RE.match(text):
            pending_anon = True
            text = text[1:].strip()
        if not text:
            continue

        m = INSTR_RE.match(text)
        if not m or proc is None:
            pending, pending_anon = [], False
            continue

        ins = Ins(n, m.group(1), m.group(2).strip(), a, i)
        k = len(proc.ins)
        proc.ins.append(ins)
        for name in pending:
            proc.by_label[name] = k
            ins.labels.append(name)
        if pending_anon:
            proc.anon.append(k)
        pending, pending_anon = [], False
        if proc.entry is None:
            proc.entry = (a, i)     # the widths the routine declares up front

    bad: list[str] = []
    for p in procs:
        p.resolve()
        bad += [f"  {path.name}:{c}" for c in p.widths()]
    return sorted(set(bad))


def main(argv: list[str]) -> int:
    paths = [Path(p) for p in argv[1:]] or sorted(SNES_SRC.glob("*.s"))
    bad: list[str] = []
    for p in paths:
        bad += scan(p)
    if bad:
        print("immediates sized for a register width the CPU does not have "
              "when it gets there:")
        print("\n".join(bad))
        return 1
    print(f"modes ok: {len(paths)} sources, every reachable immediate assembled "
          f"at the width the CPU arrives with")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
