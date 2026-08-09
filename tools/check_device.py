#!/usr/bin/env python3
"""Can the device tier be built here?  Answer precisely, and name what is missing.

    tools/check_device.py

§M7's brief opens "Requires devkitPro.  Verify first; if absent, report blocked
and stop."  Verification was six sentences of prose in platform/ds/README.md,
written once, and by the time anybody read them again two were WRONG: a Docker
client had appeared in the image, and desmume turned out to be one apt-get away.
Neither changed the answer, and that is exactly why a stale claim about a block
is dangerous -- it is right for the wrong reasons until the day it is not, and
nobody rechecks prose.

So the block is a program now.  It exits 0 only when a device build is actually
possible, which means a future agent can run it instead of believing this file,
and an environment that gains a toolchain announces itself rather than waiting to
be noticed.

WHAT A DEVICE BUILD NEEDS, and none of it is optional:

  * devkitARM -- the ARMv5TE (ARM946E-S) compiler for the ARM9 and the ARM7TDMI
    one for the ARM7.  apt's gcc-arm-none-eabi is NOT a substitute: it is built
    for the Cortex-M and Cortex-R profiles, and while it will accept
    -march=armv5te it ships no matching multilib, so nothing links.
  * libnds -- and this is the part people forget when they think "I just need a
    cross compiler".  A .nds is not a bare ELF: it needs libnds's crt0, its
    linker scripts, its specs files and its headers, plus ndstool to wrap the
    result in a cartridge header.  A compiler on its own gets you nowhere.
  * ndstool, from devkitPro's general tools.
  * an emulator or hardware, to run the thing.  desmume and melonDS are the two.

HOW TO GET IT, in the order to try:

  1. `dkp-pacman -S nds-dev`, if devkitPro's pacman is installed.
  2. devkitPro's apt repository at apt.devkitpro.org.
  3. the devkitpro/devkitarm Docker image, if a Docker DAEMON is running -- a
     client on its own is not enough and the failure looks like a permissions
     problem rather than an absent daemon.

Run it with --verbose for the reasoning behind each line.
"""
from __future__ import annotations

import argparse
import os
import shutil
import socket
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


class Probe:
    def __init__(self, what: str, why: str):
        self.what = what
        self.why = why
        self.ok = False
        self.detail = ""

    def found(self, detail: str) -> "Probe":
        self.ok, self.detail = True, detail
        return self

    def missing(self, detail: str) -> "Probe":
        self.ok, self.detail = False, detail
        return self


def probe_devkitpro() -> Probe:
    p = Probe("devkitPro", "the root of everything else")
    env = os.environ.get("DEVKITPRO", "")
    if env and Path(env).is_dir():
        return p.found(f"$DEVKITPRO={env}")
    for guess in ("/opt/devkitpro", "/usr/local/devkitpro",
                  str(Path.home() / "devkitpro")):
        if Path(guess).is_dir():
            return p.missing(f"{guess} exists but $DEVKITPRO is unset; export it")
    return p.missing("$DEVKITPRO unset and no install found in the usual places")


def probe_compiler() -> Probe:
    p = Probe("devkitARM", "the ARMv5TE compiler; the DS ARM9 is an ARM946E-S")
    dka = os.environ.get("DEVKITARM", "")
    cand = []
    if dka:
        cand.append(Path(dka) / "bin" / "arm-none-eabi-gcc")
    which = shutil.which("arm-none-eabi-gcc")
    if which:
        cand.append(Path(which))
    for c in cand:
        if not c.exists():
            continue
        try:
            v = subprocess.run([str(c), "--version"], capture_output=True,
                               text=True, timeout=20).stdout.splitlines()[0]
        except (OSError, subprocess.SubprocessError, IndexError):
            continue
        # devkitARM stamps its own name into --version.  A generic
        # arm-none-eabi-gcc from apt does not, and is the WRONG toolchain: it is
        # built for Cortex-M/R and has no ARMv5TE multilib to link against.
        if "devkitARM" in v:
            return p.found(v)
        return p.missing(f"{c} is {v.strip()}, which is not devkitARM -- apt's "
                         f"arm-none-eabi is a Cortex-M/R toolchain with no "
                         f"ARMv5TE multilib, so it compiles and does not link")
    return p.missing("no arm-none-eabi-gcc on PATH or under $DEVKITARM")


def probe_libnds() -> Probe:
    p = Probe("libnds", "crt0, linker scripts, specs and headers; a .nds is not "
                        "a bare ELF")
    root = os.environ.get("DEVKITPRO", "/opt/devkitpro")
    h = Path(root) / "libnds" / "include" / "nds.h"
    if h.is_file():
        return p.found(str(h))
    return p.missing(f"{h} absent")


def probe_ndstool() -> Probe:
    p = Probe("ndstool", "wraps the ELF in a cartridge header")
    root = os.environ.get("DEVKITPRO", "/opt/devkitpro")
    for c in (Path(root) / "tools" / "bin" / "ndstool", ):
        if c.is_file():
            return p.found(str(c))
    w = shutil.which("ndstool")
    return p.found(w) if w else p.missing("not on PATH or under $DEVKITPRO/tools")


def probe_emulator() -> Probe:
    p = Probe("an emulator", "to run the result; not needed to BUILD")
    for name in ("melonDS", "melonds", "desmume", "DeSmuME"):
        w = shutil.which(name)
        if w:
            return p.found(w)
    return p.missing("no melonDS or desmume on PATH "
                     "(desmume is in Ubuntu universe: apt-get install desmume)")


def probe_docker() -> Probe:
    p = Probe("a Docker daemon", "the devkitpro/devkitarm image is route 3")
    if not shutil.which("docker"):
        return p.missing("no docker client either")
    sock = Path("/var/run/docker.sock")
    if not sock.exists():
        return p.missing("a docker CLIENT is installed but there is no daemon "
                         "at /var/run/docker.sock -- the client alone cannot "
                         "run an image, and the error it prints reads like a "
                         "permissions problem")
    return p.found("daemon socket present")


def probe_apt_repo(timeout: float = 8.0) -> Probe:
    p = Probe("apt.devkitpro.org", "route 2, the apt repository")
    try:
        socket.gethostbyname("apt.devkitpro.org")
    except OSError as e:
        return p.missing(f"does not resolve: {e}")
    proxy = os.environ.get("HTTPS_PROXY") or os.environ.get("https_proxy")
    try:
        r = subprocess.run(
            ["curl", "-sS", "-o", "/dev/null", "-w", "%{http_code}",
             "--max-time", str(int(timeout)),
             "https://apt.devkitpro.org/dists/stable/Release"],
            capture_output=True, text=True, timeout=timeout + 5)
    except (OSError, subprocess.SubprocessError) as e:
        return p.missing(f"unreachable: {e}")
    code = r.stdout.strip()
    if code == "200":
        return p.found("reachable; dkp-pacman can be installed from it")
    via = f" through the egress proxy at {proxy}" if proxy else ""
    if code in ("403", "407"):
        # The proxy README is unambiguous about this one: a 403 is an
        # organisation egress policy denial, and the instruction is to report
        # the blocked host rather than route around it.
        return p.missing(f"HTTP {code}{via} -- an egress POLICY denial, not a "
                         f"network fault.  Report the blocked host; do not "
                         f"route around it")
    return p.missing(f"HTTP {code}{via}")


REQUIRED = ("devkitPro", "devkitARM", "libnds", "ndstool")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="print why each component is needed")
    ap.add_argument("--no-network", action="store_true",
                    help="skip the apt.devkitpro.org reachability probe")
    args = ap.parse_args(argv)

    probes = [probe_devkitpro(), probe_compiler(), probe_libnds(),
              probe_ndstool(), probe_emulator(), probe_docker()]
    if not args.no_network:
        probes.append(probe_apt_repo())

    for p in probes:
        need = " " if p.what in REQUIRED else "."
        print(f"  {'ok ' if p.ok else 'NO '}{need} {p.what:18} {p.detail}")
        if args.verbose:
            print(f"        {p.why}")

    missing = [p for p in probes if p.what in REQUIRED and not p.ok]
    if not missing:
        print("\nthe device tier can be built here.  §M7 is NOT blocked: "
              "run it, and delete the blocked note in platform/ds/README.md.")
        return 0

    print(f"\n§M7 is blocked: {len(missing)} of {len(REQUIRED)} required "
          f"components are absent ({', '.join(p.what for p in missing)}).")
    print("The host tier is unaffected -- it is most of the port and all of the "
          "risk, and it builds and tests against the oracle with no toolchain.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
