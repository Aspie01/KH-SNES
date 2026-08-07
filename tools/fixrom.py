#!/usr/bin/env python3
"""Patch the SNES internal checksum into a linked ROM image.

The header stores a 16-bit checksum at $FFDE and its ones-complement at $FFDC
(LoROM file offsets $7FDE / $7FDC).  The checksum is the sum of every byte in
the ROM, modulo 65536, computed with the complement field set to $FFFF and the
checksum field set to $0000 -- so we normalise those four bytes first.

ROMs whose size is not a power of two must be summed as if the trailing part
were repeated up to the next power of two; we handle the common mirroring case.
"""
import sys
from pathlib import Path

HDR = 0x7FB0            # LoROM header offset in the file
OFF_ROMSIZE = 0x7FD7
OFF_COMPLEMENT = 0x7FDC
OFF_CHECKSUM = 0x7FDE


def mirrored_sum(data: bytes) -> int:
    """Sum bytes, expanding a non-power-of-two image the way hardware sees it."""
    n = len(data)
    if n == 0:
        return 0
    # Largest power of two <= n
    base = 1 << (n.bit_length() - 1)
    if base == n:
        return sum(data) & 0xFFFF

    total = sum(data[:base])
    rest = data[base:]
    # The remainder is mirrored until it fills the same span as `base`.
    if rest:
        rest_sum = sum(rest)
        reps = base // len(rest) if len(rest) else 0
        total += rest_sum * reps
    return total & 0xFFFF


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: fixrom.py <rom.sfc>", file=sys.stderr)
        return 2

    path = Path(sys.argv[1])
    rom = bytearray(path.read_bytes())

    if len(rom) < 0x8000:
        print(f"error: {path} is only {len(rom)} bytes; not a valid ROM",
              file=sys.stderr)
        return 1

    # A copier header would shift every offset by 512 bytes.
    if len(rom) % 1024 == 512:
        print("error: ROM appears to carry a 512-byte copier header",
              file=sys.stderr)
        return 1

    # Normalise the checksum fields before summing.
    rom[OFF_COMPLEMENT:OFF_COMPLEMENT + 2] = b"\xFF\xFF"
    rom[OFF_CHECKSUM:OFF_CHECKSUM + 2] = b"\x00\x00"

    checksum = mirrored_sum(bytes(rom))
    complement = checksum ^ 0xFFFF

    rom[OFF_COMPLEMENT] = complement & 0xFF
    rom[OFF_COMPLEMENT + 1] = complement >> 8
    rom[OFF_CHECKSUM] = checksum & 0xFF
    rom[OFF_CHECKSUM + 1] = checksum >> 8

    path.write_bytes(bytes(rom))

    title = bytes(rom[0x7FC0:0x7FD5]).decode("ascii", "replace").rstrip()
    declared_kb = 1 << rom[OFF_ROMSIZE]
    actual_kb = len(rom) // 1024
    note = "" if declared_kb == actual_kb else \
        f"  (header declares {declared_kb} KiB)"

    print(f"{path.name}: \"{title}\"  {actual_kb} KiB  "
          f"checksum ${checksum:04X} / ${complement:04X}{note}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
