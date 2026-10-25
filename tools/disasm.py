"""Disassemble an XBE at a virtual address, with config and symbol-list names on call targets.

  python tools/disasm.py assets/xbe/default.xbe 0x1D91F8 [count] [--names config/game.toml re/xbsdb_default.txt]
  python tools/disasm.py assets/xbe/default.xbe --callers 0x1D91F8      every rel32 call or jmp to it

count is a number of instructions (default: until the first ret/jmp at the top level, at most 200).
Needs capstone (pip install capstone).
"""
import re
import struct
import sys

import capstone

ENTRY = re.compile(r"^(\w+)\s*=\s*(0x[0-9A-Fa-f]+)\b")
OTHER = re.compile(r"^(\w+?)__(\w+) = 0x([0-9a-fA-F]+)$")


def load(path):
    d = open(path, "rb").read()
    base, = struct.unpack_from("<I", d, 0x104)
    size, = struct.unpack_from("<I", d, 0x10C)
    img = bytearray(size)
    hsize, = struct.unpack_from("<I", d, 0x108)
    img[:hsize] = d[:hsize]
    n, hdr = struct.unpack_from("<II", d, 0x11C)
    secs = []
    for i in range(n):
        flags, va, vs, raw, rs, name_va = struct.unpack_from("<IIIIII", d, hdr - base + i * 56)
        k = min(vs, rs)
        img[va - base:va - base + k] = d[raw:raw + k]
        secs.append((d[name_va - base:d.index(b"\0", name_va - base)].decode(), va, vs, flags))
    return base, bytes(img), secs


def names(paths):
    out = {}
    for p in paths:
        for line in open(p):
            m = ENTRY.match(line)
            if m:
                out.setdefault(int(m.group(2), 16), m.group(1))
            m = OTHER.match(line.strip())
            if m:
                out.setdefault(int(m.group(3), 16), m.group(2))
    return out


def main(argv):
    if len(argv) < 2:
        raise SystemExit(__doc__)
    paths = []
    if "--names" in argv:
        i = argv.index("--names")
        paths, argv = argv[i + 1:], argv[:i]
    base, img, secs = load(argv[0])
    nm = names(paths)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    if argv[1] == "--callers":
        want = int(argv[2], 16)
        for name, va, vs, flags in secs:
            if not flags & 4:
                continue
            o = va - base
            for i in range(o, o + vs - 5):
                if img[i] in (0xE8, 0xE9):
                    t = (va - o + i + 5 + struct.unpack_from("<i", img, i + 1)[0]) & 0xFFFFFFFF
                    if t == want:
                        print(f"{base + i:#08x} {name} {'call' if img[i] == 0xE8 else 'jmp'}")
        return
    va = int(argv[1], 16)
    count = int(argv[2]) if len(argv) > 2 else 200
    stop_at_end = len(argv) <= 2
    furthest = va
    for n, ins in enumerate(md.disasm(img[va - base:va - base + 0x4000], va)):
        if n >= count:
            break
        note = ""
        if ins.mnemonic in ("call", "jmp") or ins.mnemonic.startswith("j"):
            try:
                t = int(ins.op_str, 16)
                note = f"  ; {nm[t]}" if t in nm else ""
                if ins.mnemonic.startswith("j"):
                    furthest = max(furthest, t)
            except ValueError:
                pass
        for m in re.findall(r"0x[0-9a-f]+", ins.op_str):
            if int(m, 16) in nm and not note:
                note = f"  ; {nm[int(m, 16)]}"
        print(f"{ins.address:#08x}  {ins.bytes.hex():24s} {ins.mnemonic} {ins.op_str}{note}")
        if stop_at_end and ins.mnemonic in ("ret", "jmp") and ins.address >= furthest:
            break


if __name__ == "__main__":
    main(sys.argv[1:])
