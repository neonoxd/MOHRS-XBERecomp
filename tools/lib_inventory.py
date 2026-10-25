"""Inventory of the library functions an XBE's game code reaches: every rel32 call target in a library section,
with the name a config or symbol list gives it and how often .text calls it.

  python tools/lib_inventory.py assets/xbe/default.xbe config/game.toml [re/xbsdb_default.txt] [--all]

Without --all only the unnamed targets .text calls directly are listed: the public library functions the game uses
that have no name yet. Needs capstone (pip install capstone).
"""
import re
import struct
import sys
from collections import Counter, defaultdict

import capstone

LIBRARY_SECTIONS = ("D3D", "DSOUND", "XGRPH", "XPP")
ENTRY = re.compile(r"^(\w+)\s*=\s*(0x[0-9A-Fa-f]+)\b")
OTHER = re.compile(r"^(\w+?)__(\w+) = 0x([0-9a-fA-F]+)$")


def sections(d):
    base, = struct.unpack_from("<I", d, 0x104)
    n, hdr = struct.unpack_from("<II", d, 0x11C)
    out = []
    for i in range(n):
        o = hdr - base + i * 56
        flags, va, vs, raw, rs, name_va = struct.unpack_from("<IIIIII", d, o)
        name = d[name_va - base:d.index(b"\0", name_va - base)].decode()
        out.append((name, va, d[raw:raw + min(vs, rs)], flags))
    return out


def names(paths):
    by_va = defaultdict(list)
    for p in paths:
        for line in open(p):
            m = ENTRY.match(line)
            if m:
                by_va[int(m.group(2), 16)].append(m.group(1))
                continue
            m = OTHER.match(line.strip())
            if m:
                by_va[int(m.group(3), 16)].append(m.group(2) + "*")  # * = from the symbol list, not the config
    return by_va


def main(argv):
    show_all = "--all" in argv
    argv = [a for a in argv if not a.startswith("--")]
    if len(argv) < 2:
        raise SystemExit(__doc__)
    d = open(argv[0], "rb").read()
    secs = sections(d)
    by_va = names(argv[1:])
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    where = lambda va: next((s[0] for s in secs if s[1] <= va < s[1] + len(s[2])), None)

    calls = defaultdict(Counter)  # target -> caller section counts
    for name, va, data, flags in secs:
        if not flags & 4 or name not in (".text",) + LIBRARY_SECTIONS:
            continue
        # linear sweep, skipping undecodable bytes; good enough to find rel32 calls
        o = 0
        while o < len(data):
            ins = next(md.disasm(data[o:o + 15], va + o, 1), None)
            if not ins:
                o += 1
                continue
            if ins.mnemonic == "call" and ins.bytes[0] == 0xE8:
                t = int(ins.op_str, 16)
                if where(t) in LIBRARY_SECTIONS:
                    calls[t][name] += 1
            o += ins.size

    rows = []
    for t in sorted(calls):
        nm = ",".join(by_va.get(t, [])) or "?"
        game = calls[t].get(".text", 0)
        if show_all or (nm == "?" and game):
            rows.append(f"{where(t):7s} {t:#08x}  from .text {game:4d}  from libs {sum(calls[t].values()) - game:4d}  {nm}")
    print("\n".join(rows))
    named = sum(1 for t in calls if t in by_va)
    print(f"{len(calls)} library call targets, {named} named, "
          f"{sum(1 for t in calls if t not in by_va and calls[t].get('.text'))} unnamed ones .text calls")


if __name__ == "__main__":
    main(sys.argv[1:])
