#!/usr/bin/env python3
"""Run the frozen SNES build headlessly and emit a per-frame trace of its WRAM.

    tools/snes_trace.py --frames 600 --input traces/idle.txt -o out/snes.trace

This is the oracle half of §M6.  The other half is tools/ds_trace.py, which runs
the DS host tier and emits the same format, and tools/trace_diff.py compares
them knowing which differences docs/behaviour/divergences/ has already accounted
for.  tools/trace_check.py runs all three over every scenario, which is the
exit criterion made executable.

THE INPUT SCRIPTS IN traces/ ARE SHARED.  The DS pad's bit assignments are the
SNES's own -- see platform/ds/include/pad.h -- so one file drives both emitters
with no translation table, and BUTTONS below is the same twelve numbers the C++
side static_asserts.

WHY NOT MEDNAFEN.  tools/playtest.sh drives the real emulator and is the right
tool for "does this look right" -- it is how the SC_PAGE bug was proved.  But its
steps are wall-clock sleeps, so it drifts, and an oracle that cannot name a frame
cannot be diffed against one.  Mednafen still has a job here: it is the
CHECKPOINT VALIDATOR for this interpreter, because a save state contains WRAM and
a state grabbed at "about frame N" can be matched against the interpreter's
window around N.  See --window.

THE FRAME MODEL, and why it is exact despite approximate timing.  The main loop
is WaitVBlank -> ReadPad -> TextUpdate -> SceneUpdate -> UpdateWorld ->
UpdateCamera -> BuildOam, and WaitVBlank spins on a WRAM byte the NMI sets.  So
the CPU does a frame's work and then parks.  Instruction costs could only matter
by moving the NMI to a different point in that work -- and if the CPU is parked,
there is no work to move it into.  --strict asserts exactly that on every frame:
that at the moment the NMI fires, the CPU has been sitting in a tight loop
writing nothing.  If it ever fails, the run stops and says which frame, because
from that frame on the trace would be timing-dependent and worthless.

WHERE A FRAME IS SAMPLED.  Immediately BEFORE the NMI fires, which is the end of
that frame's work -- the state the NMI is about to upload.  Frame 0 is the first
NMI after reset.  The DS side must sample at the same place: after its update
returns, before the next frame begins.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from snes_cpu import (Bus, Cpu, Unmapped,               # noqa: E402
                      MASTER_PER_FRAME)

ROOT = Path(__file__).resolve().parent.parent
ROM = ROOT / "platform" / "snes" / "kh.sfc"
MAP = ROOT / "platform" / "snes" / "build" / "kh.map"

# The pad, as the SNES latches it into JOY1L/H.  The high byte is B/Y/select/
# start/up/down/left/right and the low byte a/x/l/r; this is the standard
# layout and pad.s reads the 16-bit word whole.
BUTTONS = {
    "b": 0x8000, "y": 0x4000, "select": 0x2000, "start": 0x1000,
    "up": 0x0800, "down": 0x0400, "left": 0x0200, "right": 0x0100,
    "a": 0x0080, "x": 0x0040, "l": 0x0020, "r": 0x0010,
}

def load_symbols() -> dict[str, int]:
    """Zero-page and absolute WRAM symbols, from the linker's map.

    The map lists every export twice -- by name and by value -- as three columns
    of `name address flags`.  RLZ is a zero-page symbol and RLA an absolute one;
    both are WRAM here, and the address needs no bank because everything the
    trace reads lives in the first 8 KiB, which banks $00 and $7E share.
    """
    syms: dict[str, int] = {}
    text = MAP.read_text()
    body = text.split("Exports list by name:", 1)[1].split("Exports list by value:", 1)[0]
    for name, addr, kind in re.findall(r"([A-Za-z_][A-Za-z0-9_]*)\s+([0-9A-F]{6})\s+(RL[ZA])", body):
        syms[name] = int(addr, 16) & 0xFFFF
    return syms


def max_actors() -> int:
    """From game.inc, so the two never drift."""
    text = (ROOT / "platform" / "snes" / "src" / "game.inc").read_text()
    m = re.search(r"^MAX_ACTORS\s*=\s*(\d+)", text, re.M)
    if not m:
        raise SystemExit("MAX_ACTORS is not in game.inc any more")
    return int(m.group(1))


def git_revision() -> str:
    try:
        r = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT,
                           capture_output=True, text=True, timeout=10)
        rev = r.stdout.strip() or "unknown"
    except Exception:
        return "unknown"
    dirty = subprocess.run(["git", "status", "--porcelain"], cwd=ROOT,
                           capture_output=True, text=True)
    return rev + ("-dirty" if dirty.stdout.strip() else "")


def read_input_script(path: Path | None, frames: int) -> list[int]:
    """One button mask per frame.

    The file is one step per line, `buttons:frames`, with `none` for nothing --
    the same vocabulary playtest.sh uses, so a script can be read by eye and
    moved between the two.  Anything past the end of the script is no buttons.
    """
    masks = [0] * frames
    if path is None:
        return masks
    i = 0
    for raw in path.read_text().splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        keys, _, count = line.partition(":")
        n = int(count) if count.strip() else 1
        mask = 0
        for k in keys.strip().lower().split("+"):
            if k in ("none", ""):
                continue
            if k not in BUTTONS:
                raise SystemExit(f"{path}: unknown button {k!r}")
            mask |= BUTTONS[k]
        for _ in range(n):
            if i < frames:
                masks[i] = mask
            i += 1
    if i > frames:
        print(f"note: the script is {i} frames and only {frames} were asked for",
              file=sys.stderr)
    return masks



# ---------------------------------------------------------------------------
# The trace format
#
# One line per frame, tab-separated, with a header naming the platform and the
# git revision of the run.  §M6 asks for: frame number, the player's
# x/y/z/dir/state/timer/hp, then per LIVE actor its index/type/x/y/state/timer/
# hp, then the stage bytes and bossHP.
#
# Two decisions the format has to make, and both are about the two sides
# agreeing on what a column MEANS rather than on what is in it:
#
#   THE ACTOR COUNT VARIES, so the actors cannot be fixed columns.  Each live
#   actor is emitted as one field of slash-separated numbers, in SLOT ORDER --
#   which is not an arbitrary choice: the SNES updates actors in slot order and
#   allocates by first-free scan, so slot order is part of the specification and
#   a port that reordered them would be wrong for other reasons too.
#
#   THE PLAYER APPEARS TWICE, once as the named player fields and once in the
#   actor list.  That is deliberate: playerIdx is itself state, and a diff that
#   showed Sora's slot changing would otherwise have nowhere to show it.
#
# Positions are raw Q12.4 and are NOT converted to pixels.  Converting would
# hide exactly the sub-pixel divergence the oracle exists to catch.
# ---------------------------------------------------------------------------
TRACE_VERSION = 2       # v2 added the four camera columns

STAGE_BYTES = ("diveStage", "questState", "nightStage", "townStage", "sceneId")

# The camera, added in v2.  It was outside the format for as long as the format
# existed, which meant §M3's camera -- the one part of it with no hand-independent
# check -- had never been compared against anything, and divergence 001 listed
# `camY` and `bgVOfs` as the fields it excused when neither was a column at all.
# So it excused nothing and its suppression count was always zero.
#
# bgHOfs and bgVOfs are carried as well as camX/camY because they are not the
# same numbers: bgHOfs is camX plus shakeX, and the SNES's bgVOfs is
# (camY - 1) & 0x3FF for a PPU quirk the DS does not have.  Both of those are
# things a port gets wrong invisibly.
CAMERA = ("camX", "camY", "bgHOfs", "bgVOfs")


def header(platform: str, note: str = "") -> str:
    return (f"#kh-trace\tv{TRACE_VERSION}\tplatform={platform}\t"
            f"rev={git_revision()}{note}\n"
            f"#fields\tframe\tpx\tpy\tpz\tpdir\tpstate\tptimer\tphp\t"
            + "\t".join(STAGE_BYTES) + "\tbossHP\t"
            + "\t".join(CAMERA) + "\tnactors\t"
            "actor=idx/type/x/y/state/timer/hp...\n")


class Sampler:
    """Reads the trace fields out of WRAM by symbol, never by literal address."""

    def __init__(self, bus, syms: dict[str, int], max_actors: int):
        self.bus, self.s, self.n = bus, syms, max_actors

    def _b(self, name: str, i: int = 0) -> int:
        return self.bus.wram[self.s[name] + i]

    def _w(self, name: str, i: int = 0) -> int:
        a = self.s[name] + i * 2
        return self.bus.wram[a] | (self.bus.wram[a + 1] << 8)

    def line(self, frame: int) -> str:
        p = self._b("playerIdx")
        cols = [str(frame),
                str(self._w("actX", p)), str(self._w("actY", p)),
                str(self._b("actZ", p)), str(self._b("actDir", p)),
                str(self._b("actState", p)), str(self._b("actTimer", p)),
                str(self._b("actHP", p))]
        cols += [str(self._b(k)) for k in STAGE_BYTES]
        cols.append(str(self._b("bossHP")))
        cols += [str(self._w(k)) for k in CAMERA]
        live = []
        for i in range(self.n):
            t = self._b("actType", i)
            if not t:                       # slot 0 type means empty, as world.s has it
                continue
            live.append(f"{i}/{t}/{self._w('actX', i)}/{self._w('actY', i)}/"
                        f"{self._b('actState', i)}/{self._b('actTimer', i)}/"
                        f"{self._b('actHP', i)}")
        cols.append(str(len(live)))
        cols += live
        return "\t".join(cols)


class Machine:
    """The interpreter, driven one frame at a time."""

    def __init__(self, rom: bytes, strict: bool = True):
        self.bus = Bus(rom)
        self.cpu = Cpu(self.bus)
        self.strict = strict
        self.frame = -1
        self.busiest = 0
        self.worst_cost = 0
        self.reset_cost = 0
        self.worst_frame = -1
        # Generous: the reset path clears 128 KiB of WRAM through a byte port.
        self.cap = 4_000_000
        # Idleness detection, kept symbol-free on purpose: WaitVBlank is not in
        # the linker map (only some of main.s's exports are), and a test that
        # depends on a symbol would break silently if the map format changed.
        # What is actually being asked is "is the CPU doing nothing", and that
        # is answerable directly: a tight loop of PCs with no WRAM write in it.
        self._recent_pc: list[int] = []
        self._wrote = False
        real_write = self.bus.write

        def watched(bank: int, addr: int, val: int) -> None:
            b, a = bank & 0xFF, addr & 0xFFFF
            if b in (0x7E, 0x7F) or ((b <= 0x3F or 0x80 <= b <= 0xBF) and a < 0x2000):
                self._wrote = True
            real_write(bank, addr, val)

        self.bus.write = watched                       # type: ignore[method-assign]
        # vblankFlag is the byte WaitVBlank spins on; its address comes from the
        # linker map rather than being written down here, so it follows ram.s.
        self.bus.watch_addr = load_symbols()["vblankFlag"]

    def _idle(self) -> bool:
        """Is the CPU parked in WaitVBlank?

        Three conditions together, and all three are needed.  A tight loop
        alone is not enough -- ClearPpuRegs and ClearVram are tight loops too,
        and an earlier version of this test called them idle and cut every
        frame short.  What distinguishes the real one is that it is READING THE
        FLAG THE NMI SETS and writing nothing:

            @wait:  lda vblankFlag
                    beq @wait

        two instructions, five bytes, one read of $0022, no writes.
        """
        if self._wrote or not self.bus.watch_hit:
            return False
        if len(self._recent_pc) < 8:
            return False
        return max(self._recent_pc) - min(self._recent_pc) <= 8

    def run_frame(self, pad: int, sampler=None, out=None) -> int:
        """Advance until this frame's work is finished, then take the NMI.

        "Finished" is detected rather than timed: the CPU is run until it is
        idling, which is what WaitVBlank does once the frame's work is done.
        That makes the frame boundary a property of the PROGRAM rather than of
        this file's guess at instruction costs, which is what makes the trace
        exact -- see the module docstring.

        Returns the instruction count the frame took, which is the diagnostic
        that would show a frame creeping towards overrunning.
        """
        self.bus.pad = pad
        before = self.cpu.instrs
        cyc0 = self.cpu.cycles + self.bus.dma_cycles
        self._recent_pc.clear()
        self._wrote = False
        self.bus.watch_hit = False
        while not self._idle():
            if self.cpu.instrs - before > self.cap:
                raise SystemExit(
                    f"frame {self.frame + 1}: the CPU ran {self.cap} instructions "
                    f"without ever parking in WaitVBlank "
                    f"(pc=${self.cpu.pbr:02X}:{self.cpu.pc:04X}).  Either the "
                    f"frame genuinely overran -- in which case the frame model "
                    f"in this file's docstring no longer holds and the trace "
                    f"would depend on instruction timings that are only "
                    f"approximate -- or the idleness test is wrong.  Either way "
                    f"nothing after this frame is trustworthy.")
            self._recent_pc.append(self.cpu.pc)
            if len(self._recent_pc) > 16:
                self._recent_pc.pop(0)
            self.cpu.step()
            if self._wrote:
                # A write means the frame is still doing something.  The loop
                # being looked for writes nothing at all, so this resets the
                # window rather than merely noting it.
                self._wrote = False
                self._recent_pc.clear()
                self.bus.watch_hit = False
        self.frame += 1
        took = self.cpu.instrs - before
        cost = (self.cpu.cycles + self.bus.dma_cycles) - cyc0
        self.busiest = max(self.busiest, took)
        # Frame 0 is the reset path and is not a frame in this sense, so it is
        # kept apart rather than allowed to hide every normal frame behind it.
        if self.frame == 0:
            self.reset_cost = cost
        elif cost > self.worst_cost:
            self.worst_cost, self.worst_frame = cost, self.frame

        # THE ASSERTION THAT MATTERS, and it is not the one this file used to
        # make.  Running until the CPU parks and then declaring it parked proves
        # nothing -- it is true by construction.  The real question is whether
        # the frame's WORK FITS IN A FRAME, because if it does not, hardware
        # fires the NMI mid-work and WaitVBlank's `stz vblankFlag` throws that
        # flag away: one game update then consumes TWO NMIs, frameCount advances
        # by two while the logic advances by one, and `frameCount & 2` drives
        # shakeX straight into WRAM at dive.s:183, night.s:788, town.s:793 and
        # town.s:975.  The parity never recovers, so the trace is wrong from
        # there on in a way no later check would notice.
        #
        # Frame 0 is exempt: it is the reset path, which clears 128 KiB of WRAM
        # through a byte port before NMI is even armed, and is not a frame in
        # this sense at all.
        if self.strict and self.frame > 0 and cost > MASTER_PER_FRAME:
            raise SystemExit(
                f"frame {self.frame} needs {cost} master cycles and a frame is "
                f"{MASTER_PER_FRAME} ({100.0 * cost / MASTER_PER_FRAME:.0f}%).  "
                f"On hardware the NMI would land mid-work, WaitVBlank would "
                f"discard the flag, and frameCount would advance twice for one "
                f"update -- so shakeX's parity, the mote spread and the flash "
                f"palette would all diverge, permanently.  Nothing after this "
                f"frame is trustworthy.")

        # The frame's work is done and the state is what the NMI is about to
        # upload.  THIS is the sample point, and the DS side must match it:
        # after its update returns, before the next frame begins.
        if sampler is not None and out is not None:
            out.write(sampler.line(self.frame) + "\n")
        self.bus.in_vblank = True
        self.bus.nmi_flag = True
        if self.bus.nmitimen & 0x80:
            depth = self.cpu.s
            self.cpu.nmi()
            # RTI pulls back exactly what NMI pushed, so the handler is done
            # when the stack pointer comes home.  Bounded, so a handler that
            # never returns is reported rather than hanging.
            guard = 0
            while self.cpu.s < depth:
                self.cpu.step()
                guard += 1
                if guard > self.cap:
                    raise SystemExit(f"frame {self.frame}: the NMI handler did "
                                     f"not return within {self.cap} instructions")
        else:
            # Before main.s arms NMITIMEN there is no handler to run, and
            # WaitVBlank would spin for ever -- so the flag is set by hand for
            # the one frame that happens on.
            self.bus.wram[0x22] = 1
        self.bus.in_vblank = False
        return took

def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--frames", type=int, default=300)
    ap.add_argument("--input", type=Path, default=None,
                    help="button script, `keys:frames` a line")
    ap.add_argument("-o", "--out", type=Path, default=None)
    ap.add_argument("--poke", action="append", default=[],
                    metavar="[FRAME:]SYM=VAL",
                    help="write a WRAM byte, by linker-map symbol, before the "
                         "given frame runs (default 1, i.e. just after reset). "
                         "The point is to reach a scene without playing to it: "
                         "`--poke sceneId=2 --poke deadFlag=2` makes the ROM run "
                         "its OWN retry path into the Darkside fight, so the "
                         "setup is the game's code and not a hand-built state. "
                         "A later frame lets a poke land AFTER that setup has "
                         "run, which is how the race is reached: restart onto "
                         "the island first, then start the race.")
    ap.add_argument("--poke16", action="append", default=[],
                    metavar="[FRAME:]SYM=VAL",
                    help="the same, but writing a 16-bit word little-endian. "
                         "There is exactly one thing this is for and it is "
                         "rngState: the LFSR is two bytes, it is seeded by "
                         "InitWorld and by TownBegin, and a scene reached by "
                         "--poke has run neither -- so a fixture that needs the "
                         "sequence the player would have had must say which "
                         "seed it is standing in for.  A Galois register whose "
                         "state is zero stays zero, which is what makes getting "
                         "this wrong quiet rather than loud.")
    ap.add_argument("--no-strict", action="store_true",
                    help="do not stop when the CPU is still working at VBlank")
    args = ap.parse_args(argv)

    if not ROM.exists():
        raise SystemExit(f"{ROM} is not built; run make -C platform/snes")
    m = Machine(ROM.read_bytes(), strict=not args.no_strict)
    pads = read_input_script(args.input, args.frames)

    syms = load_symbols()
    sampler = Sampler(m.bus, syms, max_actors())
    out = args.out.open("w") if args.out else sys.stdout
    try:
        out.write(header("snes"))
        for n in range(args.frames):
            for spec in args.poke:
                head, _, val = spec.partition("=")
                frame, _, name = head.rpartition(":")
                when = int(frame) if frame else 1
                if when != n:
                    continue
                if name not in syms:
                    raise SystemExit(f"--poke {name}: not a symbol in kh.map")
                m.bus.wram[syms[name]] = int(val, 0) & 0xFF
                print(f"frame {n}: poked {name} (${syms[name]:04X}) = "
                      f"{int(val, 0)}", file=sys.stderr)
            for spec in args.poke16:
                head, _, val = spec.partition("=")
                frame, _, name = head.rpartition(":")
                when = int(frame) if frame else 1
                if when != n:
                    continue
                if name not in syms:
                    raise SystemExit(f"--poke16 {name}: not a symbol in kh.map")
                v = int(val, 0) & 0xFFFF
                m.bus.wram[syms[name]] = v & 0xFF
                m.bus.wram[syms[name] + 1] = v >> 8
                print(f"frame {n}: poked {name} (${syms[name]:04X}) = "
                      f"${v:04X}", file=sys.stderr)
            try:
                m.run_frame(pads[n], sampler, out)
            except Unmapped as e:
                raise SystemExit(f"frame {n}: {e}")
        print(f"ran {args.frames} frames, {m.cpu.instrs} instructions, "
              f"{m.bus.dma_bytes} DMA bytes", file=sys.stderr)
        pct = 100.0 * m.worst_cost / MASTER_PER_FRAME
        print(f"reset (frame 0, exempt): {m.reset_cost} master cycles = "
              f"{100.0 * m.reset_cost / MASTER_PER_FRAME:.0f}% of a frame",
              file=sys.stderr)
        print(f"busiest ordinary frame: #{m.worst_frame}, {m.worst_cost} master "
              f"cycles = {pct:.1f}% of a frame", file=sys.stderr)
        if pct > 70.0:
            print(f"  WARNING: that is close to the limit.  A frame over 100% "
                  f"makes the trace wrong from that frame on -- see run_frame.",
                  file=sys.stderr)
        if m.bus.unknown_reads:
            print("reads from nowhere: "
                  + ", ".join(f"${a:06X} x{n}" for a, n in
                              sorted(m.bus.unknown_reads.items())[:10]),
                  file=sys.stderr)
    finally:
        if args.out:
            out.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
