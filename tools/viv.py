"""List or extract an EA .viv archive (C0FB: big endian header, 3-byte offsets and sizes).

  python -I tools/viv.py <archive.viv>                         every file, its size and offset
  python -I tools/viv.py <archive.viv> <out dir> [.fng,.loc]   extract, only those endings when given

Get the archives from the disc image first, e.g.
  python tools/xiso.py get "assets/<image>.iso" DATA/SHELL/shell.viv re/viv/shell.viv
"""
import os
import sys


def entries(d):
    if d[0:2] != b"\xc0\xfb":
        raise SystemExit("not a C0FB archive")
    count = int.from_bytes(d[4:6], "big")
    at = 6
    out = []
    for _ in range(count):
        offset = int.from_bytes(d[at:at + 3], "big")
        size = int.from_bytes(d[at + 3:at + 6], "big")
        end = d.index(b"\0", at + 6)
        out.append((d[at + 6:end].decode("latin1"), offset, size))
        at = end + 1
    return out


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    d = open(sys.argv[1], "rb").read()
    out = sys.argv[2] if len(sys.argv) > 2 else None
    only = tuple(sys.argv[3].lower().split(",")) if len(sys.argv) > 3 else None
    for name, offset, size in entries(d):
        if not out:
            print(f"{size:9} {offset:08X} {name}")
            continue
        if only and not name.lower().endswith(only):
            continue
        # names are flat in these archives; keep them in the folder
        path = os.path.join(out, os.path.basename(name))
        os.makedirs(out, exist_ok=True)
        with open(path, "wb") as f:
            f.write(d[offset:offset + size])


if __name__ == "__main__":
    main()
