#!/usr/bin/env python3
"""A headless 65816 and just enough SNES to run this ROM and watch its WRAM.

WHY THIS EXISTS.  §M6 wants a per-frame trace from the frozen SNES build to diff
against the port.  The obvious way -- drive Mednafen and grab save states -- is
what `tools/playtest.sh` does, and its step timings are WALL-CLOCK SLEEPS, so it
drifts: it can tell you what the game looks like after "about two seconds" but
never what WRAM held on frame 137.  An oracle that cannot name a frame cannot be
diffed against one.

So the ROM runs here instead, on an interpreter that has no video, no audio and
no timing worth the name -- and does not need any of them, because of four
properties of this particular ROM, each checked rather than assumed:

  * THE APU IS NEVER TOUCHED.  APUIO0-3 are defined in snes.inc and referenced
    nowhere, so there is no SPC700 handshake to satisfy and no boot ROM to
    emulate.  This is the single thing that usually makes headless SNES
    emulation hard, and this ROM sidesteps it.

  * EXACTLY THREE REGISTERS ARE READ: HVBJOY, RDNMI and JOY1L.  Everything else
    the ROM writes is write-only to it, so a sink is a faithful model.  The one
    exception is DMA, and only because CLEAR_WRAM zeroes all 128 KiB through the
    WRAM port -- that transfer IS the initial state of every trace.

  * ALL WRAM MUTATION IS FRAME-SYNCHRONOUS.  main.s:135-144 is WaitVBlank ->
    ReadPad -> TextUpdate -> SceneUpdate -> UpdateWorld -> UpdateCamera ->
    BuildOam, and WaitVBlank spins on `vblankFlag`, a WRAM byte the NMI sets.

  * THEREFORE CYCLE ACCURACY IS ALMOST IRRELEVANT -- but not entirely, and the
    exception is the whole reason this file charges cycles at all.

    An earlier version of this docstring claimed the frame model was exact
    "provided the CPU is parked in the spin loop when the NMI arrives", and
    asserted exactly that.  THE ASSERTION WAS VACUOUS: the driver runs until the
    CPU parks and then declares it parked.  It proved nothing.

    The condition that actually matters is different.  If a frame's work exceeds
    a frame, hardware fires the NMI mid-work -- and WaitVBlank's opening
    `stz vblankFlag` then THROWS THAT FLAG AWAY, so one game update consumes two
    NMIs.  frameCount advances by two while the logic advances by one, and
    frameCount is not cosmetic: `frameCount & 2` picks shakeX at dive.s:183,
    night.s:788, town.s:793 and town.s:975, the flash palette at oam.s:292, the
    mote spread at dive.s:512 and the dark column's cel at night.s:577.  A single
    swallowed NMI flips a parity that never recovers.

    So the real question is "does the work fit in a frame", and answering it
    needs a cost model -- dominated not by instructions but by DMA, at a fixed
    eight master cycles a byte.  A LoadScene call moves about 31 KiB, which is
    70% of a frame on its own.  Ordinary frames measure around 32%; the check is
    in run_frame() and the run stops rather than emitting a trace it cannot
    stand behind.

WHAT IS DELIBERATELY NOT MODELLED, and why it is safe: VRAM, CGRAM and OAM.  The
ROM never reads them back, and no trace field lives there.  DMA to those targets
counts its bytes and discards them, which keeps the DMA registers honest without
pretending to a video state nobody looks at.

THE DECODER HARD-ERRORS ON AN UNKNOWN OPCODE.  tools/snes_opcodes.py holds the
89 bytes ca65 actually emits for the frozen sources, checked against the
assembler rather than against a reference matrix.  An unknown byte therefore
means the decoder desynchronised, and it is reported at the instruction that
caused it instead of as a corrupt trace two hundred frames later.
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from snes_opcodes import OPCODES, FIXED_LEN            # noqa: E402

# Master cycles per CPU cycle on FastROM (3.58 MHz against the 21.47 MHz master
# clock).  WRAM and the register file are slow-bus and cost 8 rather than 6, but
# that difference is inside this estimate's error bars and the frame check that
# uses it carries margin -- see CYCLES_PER_MODE.
MASTER_PER_CPU = 6

# CPU cycles by addressing mode: the standard 65816 counts, +1 when the operand
# is sixteen bits wide.  AN ESTIMATE, and deliberately labelled as one: nothing
# in the trace depends on it.  The ONE thing it is used for is asking whether a
# frame's work fits inside a frame, which is a question about a ~1% boolean at a
# ~10% margin, so an estimate answers it and a wrong answer is visible as a
# frame near the limit rather than as a silently wrong trace.
CYCLES_PER_MODE = {
    "imp": 2, "acc": 2, "imm": 2, "imm8": 3, "rel": 2,
    "dp": 3, "dp_x": 4, "dp_y": 4, "abs": 4, "abs_x": 4, "abs_y": 4,
    "ind_y": 5, "lng": 6, "lng_y": 6,
}
# The ones whose cost is nothing like their addressing mode.
CYCLES_SPECIAL = {
    "jsr": 6, "rts": 6, "rti": 7, "jmp": 3,
    "pha": 3, "phx": 3, "phy": 3, "phb": 3, "phk": 3, "phd": 4,
    "pla": 4, "plx": 4, "ply": 4, "plb": 4, "pld": 5,
}

# fullsnes: DMA costs "fixed 8 master cycles per byte", independent of the
# A-bus bank's speed.  This is the DOMINANT term on any frame that calls
# LoadScene -- the Dive path moves 31304 bytes, which is 250432 master cycles.
MASTER_PER_DMA_BYTE = 8

# One NTSC frame: 262 scanlines of 1364 master cycles.
MASTER_PER_FRAME = 262 * 1364

ROOT = Path(__file__).resolve().parent.parent

# P flag bits.  In native mode bit 5 is the accumulator width and bit 4 the
# index width, and in both cases 1 MEANS EIGHT BITS -- the sense that reads
# backwards and is worth naming rather than remembering.
FLAG_C, FLAG_Z, FLAG_I, FLAG_D = 0x01, 0x02, 0x04, 0x08
FLAG_X, FLAG_M, FLAG_V, FLAG_N = 0x10, 0x20, 0x40, 0x80


class Unmapped(Exception):
    """A read from somewhere this ROM is not supposed to touch."""


class Bus:
    """The memory map, and the four registers that are not sinks.

    LoROM: banks $00-$3F and $80-$BF carry the WRAM mirror at $0000-$1FFF, the
    B-bus at $2100-$21FF, the CPU registers at $4200-$44FF, and ROM at
    $8000-$FFFF.  Banks $7E-$7F are the 128 KiB of WRAM itself.
    """

    def __init__(self, rom: bytes):
        self.rom = rom
        self.wram = bytearray(128 * 1024)
        # The three readable registers' state.
        self.nmi_flag = False           # RDNMI bit 7, cleared by reading it
        self.in_vblank = False          # HVBJOY bit 7
        self.nmitimen = 0
        self.pad = 0                    # JOY1L/H, filled by auto-joypad read
        # DMA channel 0.  Only channel 0 is ever used.
        self.dmap = self.bbad = self.a1b = 0
        self.a1t = self.das = 0
        self.wmadd = 0                  # the WRAM port's own address
        self.dma_bytes = 0
        self.dma_cycles = 0             # 8 master cycles a byte, and it dominates
        self.unknown_reads: dict[int, int] = {}
        # The driver watches one WRAM address to know when the CPU is waiting
        # for VBlank rather than merely looping.  Set by Machine; -1 disables.
        self.watch_addr = -1
        self.watch_hit = False

    # --- address decoding ---------------------------------------------------
    @staticmethod
    def _rom_off(bank: int, addr: int) -> int:
        return ((bank & 0x7F) * 0x8000) + (addr - 0x8000)

    def read(self, bank: int, addr: int) -> int:
        bank &= 0xFF
        addr &= 0xFFFF
        if bank in (0x7E, 0x7F):
            off = ((bank - 0x7E) << 16) | addr
            if off == self.watch_addr:
                self.watch_hit = True
            return self.wram[off]
        if (bank <= 0x3F or 0x80 <= bank <= 0xBF):
            if addr < 0x2000:
                if addr == self.watch_addr:
                    self.watch_hit = True
                return self.wram[addr]
            if 0x2100 <= addr <= 0x21FF or 0x4200 <= addr <= 0x44FF:
                return self._read_reg(addr)
            if addr >= 0x8000:
                return self.rom[self._rom_off(bank, addr)]
            # $2200-$3FFF and $4000-$41FF: nothing here, and this ROM has no
            # business reading it.  Counted rather than silently zero, so a
            # desynchronised decoder shows up as a pile of odd addresses.
            self.unknown_reads[(bank << 16) | addr] = \
                self.unknown_reads.get((bank << 16) | addr, 0) + 1
            return 0
        if addr >= 0x8000:
            return self.rom[self._rom_off(bank, addr)]
        self.unknown_reads[(bank << 16) | addr] = \
            self.unknown_reads.get((bank << 16) | addr, 0) + 1
        return 0

    def write(self, bank: int, addr: int, val: int) -> None:
        bank &= 0xFF
        addr &= 0xFFFF
        val &= 0xFF
        if bank in (0x7E, 0x7F):
            self.wram[((bank - 0x7E) << 16) | addr] = val
            return
        if bank <= 0x3F or 0x80 <= bank <= 0xBF:
            if addr < 0x2000:
                self.wram[addr] = val
                return
            if 0x2100 <= addr <= 0x21FF or 0x4200 <= addr <= 0x44FF:
                self._write_reg(addr, val)
                return
        # ROM and everything else: writes go nowhere, exactly as on hardware.

    # --- the registers that are not sinks -----------------------------------
    def _read_reg(self, addr: int) -> int:
        if addr == 0x4210:              # RDNMI
            # Bit 7 is the NMI flag AND READING CLEARS IT.  The low nibble is
            # the CPU version, 2 on every retail unit; the ROM ignores it but
            # returning 0 would still be a lie.
            v = (0x80 if self.nmi_flag else 0x00) | 0x02
            self.nmi_flag = False
            return v
        if addr == 0x4212:              # HVBJOY
            # Bit 7 vblank, bit 6 hblank, bit 0 auto-joypad busy.  Auto-joypad
            # is modelled as instantaneous, so bit 0 never reads busy -- which
            # is a lie the ROM cannot detect, because it never polls it.
            return 0x80 if self.in_vblank else 0x00
        if addr == 0x4218:
            return self.pad & 0xFF
        if addr == 0x4219:
            return (self.pad >> 8) & 0xFF
        # Every other register in range is write-only to this ROM.  Reading one
        # would mean the decoder has gone astray, so it is counted.
        self.unknown_reads[addr] = self.unknown_reads.get(addr, 0) + 1
        return 0

    def _write_reg(self, addr: int, val: int) -> None:
        if addr == 0x4200:
            self.nmitimen = val
        elif addr == 0x4300:
            self.dmap = val
        elif addr == 0x4301:
            self.bbad = val
        elif addr == 0x4302:
            self.a1t = (self.a1t & 0xFF00) | val
        elif addr == 0x4303:
            self.a1t = (self.a1t & 0x00FF) | (val << 8)
        elif addr == 0x4304:
            self.a1b = val
        elif addr == 0x4305:
            self.das = (self.das & 0xFF00) | val
        elif addr == 0x4306:
            self.das = (self.das & 0x00FF) | (val << 8)
        elif addr == 0x420B:
            if val & 0x01:
                self._run_dma()
        elif addr == 0x2181:
            self.wmadd = (self.wmadd & 0x1FF00) | val
        elif addr == 0x2182:
            self.wmadd = (self.wmadd & 0x100FF) | (val << 8)
        elif addr == 0x2183:
            self.wmadd = (self.wmadd & 0x0FFFF) | ((val & 1) << 16)
        elif addr == 0x2180:
            self.wram[self.wmadd] = val
            self.wmadd = (self.wmadd + 1) & 0x1FFFF
        # Everything else is a sink: VRAM, CGRAM, OAM and the PPU's own
        # configuration, none of which this trace looks at.

    def _run_dma(self) -> None:
        """General-purpose DMA on channel 0.

        Only the two things this ROM does are implemented faithfully: a fixed
        source writing one byte repeatedly (CLEAR_WRAM), and an incrementing
        source streaming to a B-bus port.  The transfer's DESTINATION only
        matters when it is the WRAM port, because nothing else it can reach is
        in the trace -- so a transfer to VRAM still walks its source addresses
        and counts its bytes, and then discards them.
        """
        count = self.das if self.das else 0x10000     # 0 means 65536
        fixed = bool(self.dmap & 0x08)                # A-bus address fixed
        step = 0 if fixed else (-1 if self.dmap & 0x10 else 1)
        mode = self.dmap & 0x07
        # Mode 0 is one byte to one register; 1 is two bytes to two registers;
        # 2 and 6 are two bytes to one; 3, 4, 5, 7 exist and this ROM does not
        # use them.  Only the B-bus offset pattern differs, and the only port
        # whose offset matters here is $2180, which is a single register.
        pattern = {0: (0,), 1: (0, 1), 2: (0, 0), 6: (0, 0)}.get(mode, (0,))
        src = self.a1t
        i = 0
        while count > 0:
            b = self.read(self.a1b, src)
            port = 0x2100 | ((self.bbad + pattern[i % len(pattern)]) & 0xFF)
            self._write_reg(port, b)
            src = (src + step) & 0xFFFF
            count -= 1
            i += 1
            self.dma_bytes += 1
            self.dma_cycles += MASTER_PER_DMA_BYTE
        self.a1t = src
        self.das = 0


class Cpu:
    """The 65816, in native mode, binary arithmetic only.

    The ROM contains no `sed` and no `cld`, and reset clears D, so decimal mode
    is unreachable and is not implemented -- an `adc` in decimal mode would be
    silently wrong, so the flag is asserted clear instead of being ignored.
    """

    def __init__(self, bus: Bus):
        self.bus = bus
        self.c = 0          # the full 16-bit accumulator; A is its low half
        self.x = self.y = 0
        self.s = 0x01FF
        self.d = 0
        self.pbr = 0
        self.dbr = 0
        self.p = FLAG_M | FLAG_X | FLAG_I
        self.e = True       # emulation mode; the ROM's third instruction leaves it
        self.pc = 0
        self.cycles = 0
        self.instrs = 0
        self.reset()

    def reset(self) -> None:
        self.pc = self.bus.read(0x00, 0xFFFC) | (self.bus.read(0x00, 0xFFFD) << 8)
        self.pbr = 0

    # --- register width helpers ---------------------------------------------
    @property
    def m8(self) -> bool:
        return self.e or bool(self.p & FLAG_M)

    @property
    def x8(self) -> bool:
        return self.e or bool(self.p & FLAG_X)

    @property
    def a(self) -> int:
        return self.c & 0xFF

    def set_a(self, v: int) -> None:
        if self.m8:
            self.c = (self.c & 0xFF00) | (v & 0xFF)
        else:
            self.c = v & 0xFFFF

    def _nz(self, v: int, eight: bool) -> None:
        self.p &= ~(FLAG_N | FLAG_Z)
        if eight:
            if not v & 0xFF:
                self.p |= FLAG_Z
            if v & 0x80:
                self.p |= FLAG_N
        else:
            if not v & 0xFFFF:
                self.p |= FLAG_Z
            if v & 0x8000:
                self.p |= FLAG_N

    # --- fetch --------------------------------------------------------------
    def _fetch8(self) -> int:
        v = self.bus.read(self.pbr, self.pc)
        self.pc = (self.pc + 1) & 0xFFFF
        return v

    def _fetch16(self) -> int:
        return self._fetch8() | (self._fetch8() << 8)

    # --- memory access at the current width ---------------------------------
    def _read(self, bank: int, addr: int, eight: bool) -> int:
        lo = self.bus.read(bank, addr)
        if eight:
            return lo
        return lo | (self.bus.read(bank, (addr + 1) & 0xFFFF) << 8)

    def _write(self, bank: int, addr: int, val: int, eight: bool) -> None:
        self.bus.write(bank, addr, val & 0xFF)
        if not eight:
            self.bus.write(bank, (addr + 1) & 0xFFFF, (val >> 8) & 0xFF)

    # --- stack --------------------------------------------------------------
    def _push8(self, v: int) -> None:
        self.bus.write(0x00, self.s, v & 0xFF)
        self.s = (self.s - 1) & 0xFFFF

    def _pull8(self) -> int:
        self.s = (self.s + 1) & 0xFFFF
        return self.bus.read(0x00, self.s)

    def _push16(self, v: int) -> None:
        self._push8((v >> 8) & 0xFF)
        self._push8(v & 0xFF)

    def _pull16(self) -> int:
        return self._pull8() | (self._pull8() << 8)

    # --- effective addresses ------------------------------------------------
    def _ea(self, mode: str) -> tuple[int, int]:
        """(bank, address) for the data this instruction touches."""
        if mode == "dp":
            return 0x00, (self.d + self._fetch8()) & 0xFFFF
        if mode == "dp_x":
            return 0x00, (self.d + self._fetch8() + self.x) & 0xFFFF
        if mode == "dp_y":
            return 0x00, (self.d + self._fetch8() + self.y) & 0xFFFF
        if mode == "abs":
            return self.dbr, self._fetch16()
        if mode in ("abs_x", "abs_y"):
            # Absolute indexed CARRIES INTO THE BANK on the 65816, unlike the
            # direct-page forms which wrap inside bank 0.
            base = (self.dbr << 16) | self._fetch16()
            full = (base + (self.x if mode == "abs_x" else self.y)) & 0xFFFFFF
            return full >> 16, full & 0xFFFF
        if mode == "ind_y":
            p = (self.d + self._fetch8()) & 0xFFFF
            ptr = self.bus.read(0x00, p) | (self.bus.read(0x00, (p + 1) & 0xFFFF) << 8)
            full = (((self.dbr << 16) | ptr) + self.y) & 0xFFFFFF
            return full >> 16, full & 0xFFFF
        if mode in ("lng", "lng_y"):
            p = (self.d + self._fetch8()) & 0xFFFF
            ptr = (self.bus.read(0x00, p)
                   | (self.bus.read(0x00, (p + 1) & 0xFFFF) << 8)
                   | (self.bus.read(0x00, (p + 2) & 0xFFFF) << 16))
            if mode == "lng_y":
                ptr = (ptr + self.y) & 0xFFFFFF
            return ptr >> 16, ptr & 0xFFFF
        raise AssertionError(f"no effective address for mode {mode}")

    def _operand(self, mnemonic: str, mode: str, eight: bool) -> int:
        if mode == "imm":
            return self._fetch8() if eight else self._fetch16()
        bank, addr = self._ea(mode)
        return self._read(bank, addr, eight)

    # --- the interpreter ----------------------------------------------------
    def step(self) -> None:
        op = self._fetch8()
        entry = OPCODES.get(op)
        if entry is None:
            raise Unmapped(
                f"opcode ${op:02X} at ${self.pbr:02X}:{(self.pc - 1) & 0xFFFF:04X} "
                f"is not one of the 89 the assembler emits -- the decoder has "
                f"desynchronised, or the CPU is running data")
        mnem, mode = entry
        self.instrs += 1
        n = CYCLES_SPECIAL.get(mnem, CYCLES_PER_MODE[mode])
        if mode in ("imm", "dp", "dp_x", "abs", "abs_x", "abs_y", "ind_y",
                    "lng", "lng_y") and not (self.m8 if mnem not in
                    ("ldx", "ldy", "cpx", "cpy", "stx", "sty") else self.x8):
            n += 1              # sixteen-bit operand: one more bus access
        self.cycles += n * MASTER_PER_CPU
        getattr(self, "_op_" + mnem)(mode)

    # --- loads and stores ---------------------------------------------------
    def _op_lda(self, mode: str) -> None:
        v = self._operand("lda", mode, self.m8)
        self.set_a(v)
        self._nz(v, self.m8)

    def _op_ldx(self, mode: str) -> None:
        v = self._operand("ldx", mode, self.x8)
        self.x = v & (0xFF if self.x8 else 0xFFFF)
        self._nz(self.x, self.x8)

    def _op_ldy(self, mode: str) -> None:
        v = self._operand("ldy", mode, self.x8)
        self.y = v & (0xFF if self.x8 else 0xFFFF)
        self._nz(self.y, self.x8)

    def _op_sta(self, mode: str) -> None:
        bank, addr = self._ea(mode)
        self._write(bank, addr, self.c, self.m8)

    def _op_stx(self, mode: str) -> None:
        bank, addr = self._ea(mode)
        self._write(bank, addr, self.x, self.x8)

    def _op_sty(self, mode: str) -> None:
        bank, addr = self._ea(mode)
        self._write(bank, addr, self.y, self.x8)

    def _op_stz(self, mode: str) -> None:
        bank, addr = self._ea(mode)
        self._write(bank, addr, 0, self.m8)

    # --- arithmetic ---------------------------------------------------------
    def _op_adc(self, mode: str) -> None:
        assert not self.p & FLAG_D, "decimal mode is not implemented and this ROM never enters it"
        v = self._operand("adc", mode, self.m8)
        if self.m8:
            a = self.a
            r = a + v + (1 if self.p & FLAG_C else 0)
            self.p = (self.p | FLAG_C) if r > 0xFF else (self.p & ~FLAG_C)
            ov = (~(a ^ v) & (a ^ r)) & 0x80
        else:
            a = self.c
            r = a + v + (1 if self.p & FLAG_C else 0)
            self.p = (self.p | FLAG_C) if r > 0xFFFF else (self.p & ~FLAG_C)
            ov = (~(a ^ v) & (a ^ r)) & 0x8000
        self.p = (self.p | FLAG_V) if ov else (self.p & ~FLAG_V)
        self.set_a(r)
        self._nz(r, self.m8)

    def _op_sbc(self, mode: str) -> None:
        assert not self.p & FLAG_D, "decimal mode is not implemented and this ROM never enters it"
        v = self._operand("sbc", mode, self.m8)
        # SBC is ADC of the ones' complement: carry SET means no borrow.
        if self.m8:
            a, v = self.a, v ^ 0xFF
            r = a + v + (1 if self.p & FLAG_C else 0)
            self.p = (self.p | FLAG_C) if r > 0xFF else (self.p & ~FLAG_C)
            ov = (~(a ^ v) & (a ^ r)) & 0x80
        else:
            a, v = self.c, v ^ 0xFFFF
            r = a + v + (1 if self.p & FLAG_C else 0)
            self.p = (self.p | FLAG_C) if r > 0xFFFF else (self.p & ~FLAG_C)
            ov = (~(a ^ v) & (a ^ r)) & 0x8000
        self.p = (self.p | FLAG_V) if ov else (self.p & ~FLAG_V)
        self.set_a(r)
        self._nz(r, self.m8)

    def _compare(self, reg: int, v: int, eight: bool) -> None:
        mask = 0xFF if eight else 0xFFFF
        r = (reg & mask) - (v & mask)
        self.p = (self.p | FLAG_C) if r >= 0 else (self.p & ~FLAG_C)
        self._nz(r & mask, eight)

    def _op_cmp(self, mode: str) -> None:
        self._compare(self.c, self._operand("cmp", mode, self.m8), self.m8)

    def _op_cpx(self, mode: str) -> None:
        self._compare(self.x, self._operand("cpx", mode, self.x8), self.x8)

    def _op_cpy(self, mode: str) -> None:
        self._compare(self.y, self._operand("cpy", mode, self.x8), self.x8)

    # --- logic --------------------------------------------------------------
    def _logic(self, mode: str, mnem: str, f) -> None:
        v = self._operand(mnem, mode, self.m8)
        r = f(self.c & (0xFF if self.m8 else 0xFFFF), v)
        self.set_a(r)
        self._nz(r, self.m8)

    def _op_and(self, mode: str) -> None:
        self._logic(mode, "and", lambda a, v: a & v)

    def _op_ora(self, mode: str) -> None:
        self._logic(mode, "ora", lambda a, v: a | v)

    def _op_eor(self, mode: str) -> None:
        self._logic(mode, "eor", lambda a, v: a ^ v)

    # --- shifts -------------------------------------------------------------
    def _op_asl(self, mode: str) -> None:
        top = 0x80 if self.m8 else 0x8000
        if mode == "acc":
            v = self.c & (0xFF if self.m8 else 0xFFFF)
            self.p = (self.p | FLAG_C) if v & top else (self.p & ~FLAG_C)
            r = (v << 1) & (0xFF if self.m8 else 0xFFFF)
            self.set_a(r)
        else:
            bank, addr = self._ea(mode)
            v = self._read(bank, addr, self.m8)
            self.p = (self.p | FLAG_C) if v & top else (self.p & ~FLAG_C)
            r = (v << 1) & (0xFF if self.m8 else 0xFFFF)
            self._write(bank, addr, r, self.m8)
        self._nz(r, self.m8)

    def _op_ror(self, mode: str) -> None:
        """Rotate right through carry -- and in this ROM, always an ASR.

        `ror` appears only inside the ASR1 macro (macros.inc:49-52), which is
        `cmp #$8000 / ror a`: the compare puts the sign bit into carry and the
        rotate brings it back into bit 15, so the pair is an arithmetic shift
        right.  That is why grid.h's asr1() floors, why a Shadow's westward step
        is 9 against an eastward 8, and why -1 is a fixed point of it.
        """
        top = 0x80 if self.m8 else 0x8000
        carry_in = top if self.p & FLAG_C else 0
        if mode == "acc":
            v = self.c & (0xFF if self.m8 else 0xFFFF)
            self.p = (self.p | FLAG_C) if v & 1 else (self.p & ~FLAG_C)
            r = (v >> 1) | carry_in
            self.set_a(r)
        else:
            bank, addr = self._ea(mode)
            v = self._read(bank, addr, self.m8)
            self.p = (self.p | FLAG_C) if v & 1 else (self.p & ~FLAG_C)
            r = (v >> 1) | carry_in
            self._write(bank, addr, r, self.m8)
        self._nz(r, self.m8)

    def _op_lsr(self, mode: str) -> None:
        if mode == "acc":
            v = self.c & (0xFF if self.m8 else 0xFFFF)
            self.p = (self.p | FLAG_C) if v & 1 else (self.p & ~FLAG_C)
            r = v >> 1
            self.set_a(r)
        else:
            bank, addr = self._ea(mode)
            v = self._read(bank, addr, self.m8)
            self.p = (self.p | FLAG_C) if v & 1 else (self.p & ~FLAG_C)
            r = v >> 1
            self._write(bank, addr, r, self.m8)
        self._nz(r, self.m8)

    # --- increment and decrement --------------------------------------------
    # INC/DEC touch N and Z and NOT carry.  On memory they do not touch the
    # accumulator either, which is the detail that produced finding 44: a
    # `dec dayTimer` followed by `lsr a` halves the value loaded BEFORE the
    # decrement, because the decrement never reached the accumulator.
    def _op_inc(self, mode: str) -> None:
        if mode == "acc":
            r = (self.c + 1) & (0xFF if self.m8 else 0xFFFF)
            self.set_a(r)
        else:
            bank, addr = self._ea(mode)
            r = (self._read(bank, addr, self.m8) + 1) & (0xFF if self.m8 else 0xFFFF)
            self._write(bank, addr, r, self.m8)
        self._nz(r, self.m8)

    def _op_dec(self, mode: str) -> None:
        if mode == "acc":
            r = (self.c - 1) & (0xFF if self.m8 else 0xFFFF)
            self.set_a(r)
        else:
            bank, addr = self._ea(mode)
            r = (self._read(bank, addr, self.m8) - 1) & (0xFF if self.m8 else 0xFFFF)
            self._write(bank, addr, r, self.m8)
        self._nz(r, self.m8)

    def _op_inx(self, _m: str) -> None:
        self.x = (self.x + 1) & (0xFF if self.x8 else 0xFFFF)
        self._nz(self.x, self.x8)

    def _op_iny(self, _m: str) -> None:
        self.y = (self.y + 1) & (0xFF if self.x8 else 0xFFFF)
        self._nz(self.y, self.x8)

    def _op_dey(self, _m: str) -> None:
        self.y = (self.y - 1) & (0xFF if self.x8 else 0xFFFF)
        self._nz(self.y, self.x8)

    # --- transfers ----------------------------------------------------------
    # The width rule that catches people: a transfer INTO an index register
    # while x=1 leaves only eight bits, and TXA/TYA with m=0 move the WHOLE
    # index register even if x=1, in which case the high byte is zero.
    def _op_tax(self, _m: str) -> None:
        self.x = self.c & (0xFF if self.x8 else 0xFFFF)
        self._nz(self.x, self.x8)

    def _op_tay(self, _m: str) -> None:
        self.y = self.c & (0xFF if self.x8 else 0xFFFF)
        self._nz(self.y, self.x8)

    def _op_txa(self, _m: str) -> None:
        self.set_a(self.x)
        self._nz(self.x, self.m8)

    def _op_tya(self, _m: str) -> None:
        self.set_a(self.y)
        self._nz(self.y, self.m8)

    def _op_txs(self, _m: str) -> None:
        # TXS sets NO flags, and in native mode the stack pointer is 16-bit.
        self.s = self.x & 0xFFFF

    def _op_tcd(self, _m: str) -> None:
        # Always the full 16-bit C, whatever m says, and always 16-bit flags.
        self.d = self.c & 0xFFFF
        self._nz(self.d, False)

    def _op_xba(self, _m: str) -> None:
        self.c = ((self.c << 8) | (self.c >> 8)) & 0xFFFF
        # Flags come from the NEW low byte, eight bits, whatever m says.
        self._nz(self.c & 0xFF, True)

    def _op_xce(self, _m: str) -> None:
        carry = bool(self.p & FLAG_C)
        self.p = (self.p | FLAG_C) if self.e else (self.p & ~FLAG_C)
        self.e = carry
        if self.e:
            self.p |= FLAG_M | FLAG_X
            self.x &= 0xFF
            self.y &= 0xFF
            self.s = 0x0100 | (self.s & 0xFF)

    # --- stack --------------------------------------------------------------
    def _op_pha(self, _m: str) -> None:
        self._push8(self.c) if self.m8 else self._push16(self.c)

    def _op_pla(self, _m: str) -> None:
        v = self._pull8() if self.m8 else self._pull16()
        self.set_a(v)
        self._nz(v, self.m8)

    def _op_phx(self, _m: str) -> None:
        self._push8(self.x) if self.x8 else self._push16(self.x)

    def _op_plx(self, _m: str) -> None:
        self.x = self._pull8() if self.x8 else self._pull16()
        self._nz(self.x, self.x8)

    def _op_phy(self, _m: str) -> None:
        self._push8(self.y) if self.x8 else self._push16(self.y)

    def _op_ply(self, _m: str) -> None:
        self.y = self._pull8() if self.x8 else self._pull16()
        self._nz(self.y, self.x8)

    def _op_phb(self, _m: str) -> None:
        self._push8(self.dbr)

    def _op_plb(self, _m: str) -> None:
        self.dbr = self._pull8()
        self._nz(self.dbr, True)

    def _op_phd(self, _m: str) -> None:
        self._push16(self.d)

    def _op_pld(self, _m: str) -> None:
        self.d = self._pull16()
        self._nz(self.d, False)

    def _op_phk(self, _m: str) -> None:
        self._push8(self.pbr)

    # --- flags --------------------------------------------------------------
    def _op_clc(self, _m: str) -> None:
        self.p &= ~FLAG_C

    def _op_sec(self, _m: str) -> None:
        self.p |= FLAG_C

    def _op_sei(self, _m: str) -> None:
        self.p |= FLAG_I

    def _op_cli(self, _m: str) -> None:
        self.p &= ~FLAG_I

    def _op_rep(self, _m: str) -> None:
        self.p &= ~self._fetch8()
        self._after_width_change()

    def _op_sep(self, _m: str) -> None:
        self.p |= self._fetch8()
        self._after_width_change()

    def _after_width_change(self) -> None:
        # THE HIGH BYTES OF X AND Y ARE LOST when the index registers narrow.
        # They are not restored when the registers widen again: the bits are
        # gone.  This is the single most common way an interpreter drifts from
        # hardware in a way that looks like a game bug.
        if self.x8:
            self.x &= 0xFF
            self.y &= 0xFF

    # --- control flow -------------------------------------------------------
    def _op_jmp(self, _m: str) -> None:
        self.pc = self._fetch16()

    def _op_jsr(self, _m: str) -> None:
        target = self._fetch16()
        # What is pushed is the address of the instruction's LAST BYTE, so RTS
        # has to add one to it.
        self._push16((self.pc - 1) & 0xFFFF)
        self.pc = target

    def _op_rts(self, _m: str) -> None:
        self.pc = (self._pull16() + 1) & 0xFFFF

    def _op_rti(self, _m: str) -> None:
        self.p = self._pull8()
        self.pc = self._pull16()
        if not self.e:
            self.pbr = self._pull8()
        self._after_width_change()

    def _branch(self, taken: bool) -> None:
        off = self._fetch8()
        if off & 0x80:
            off -= 0x100
        if taken:
            self.pc = (self.pc + off) & 0xFFFF

    def _op_bra(self, _m: str) -> None:
        self._branch(True)

    def _op_beq(self, _m: str) -> None:
        self._branch(bool(self.p & FLAG_Z))

    def _op_bne(self, _m: str) -> None:
        self._branch(not self.p & FLAG_Z)

    def _op_bcs(self, _m: str) -> None:
        self._branch(bool(self.p & FLAG_C))

    def _op_bcc(self, _m: str) -> None:
        self._branch(not self.p & FLAG_C)

    def _op_bmi(self, _m: str) -> None:
        self._branch(bool(self.p & FLAG_N))

    def _op_bpl(self, _m: str) -> None:
        self._branch(not self.p & FLAG_N)

    # --- interrupts ---------------------------------------------------------
    def nmi(self) -> None:
        """Native-mode NMI: push PBR, PC, P; vector through $00FFEA."""
        self._push8(self.pbr)
        self._push16(self.pc)
        self._push8(self.p)
        self.p |= FLAG_I
        self.p &= ~FLAG_D
        self.pbr = 0
        self.pc = self.bus.read(0x00, 0xFFEA) | (self.bus.read(0x00, 0xFFEB) << 8)
