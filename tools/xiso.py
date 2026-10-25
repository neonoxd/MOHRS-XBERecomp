"""List or extract files from an Xbox disc image (XDVDFS / XISO).

Works on a game partition image or a full XGD1/XGD2/XGD3 redump.

  python tools/xiso.py list  <image.iso>
  python tools/xiso.py get   <image.iso> <disc path> <out file>     e.g. default.xbe assets/xbe/default.xbe
  python tools/xiso.py xbes  <image.iso> <out dir>                  every .xbe in the root
  python tools/xiso.py extract <image.iso> <out dir>                the whole disc
"""
import os
import struct
import sys

MAGIC = b"MICROSOFT*XBOX*MEDIA"
SECTOR = 2048
# game partition offsets: plain image, XGD1, XGD2, XGD3 redumps
PARTITIONS = (0, 0x18300000, 0xFD90000, 0x2080000)


class Xiso:
    def __init__(self, path):
        self.f = open(path, "rb")
        self.base = None
        for off in PARTITIONS:
            self.f.seek(off + 32 * SECTOR)
            if self.f.read(len(MAGIC)) == MAGIC:
                self.base = off
                break
        if self.base is None:
            raise SystemExit(f"{path}: no XDVDFS volume found")
        self.root_sector, self.root_size = struct.unpack("<II", self.f.read(8))

    def read(self, sector, size):
        self.f.seek(self.base + sector * SECTOR)
        return self.f.read(size)

    def entries(self, sector=None, size=None, prefix=""):
        """(path, sector, size, is_dir) for every entry, depth first, in tree order."""
        if sector is None:
            sector, size = self.root_sector, self.root_size
        if size == 0:
            return
        data = self.read(sector, size)
        stack = [0]
        seen = set()
        while stack:
            o = stack.pop()
            if o in seen or o * 4 + 14 > len(data):
                continue
            seen.add(o)
            left, right, s, sz, attr, nlen = struct.unpack_from("<HHIIBB", data, o * 4)
            if left == 0xFFFF:
                continue
            name = data[o * 4 + 14:o * 4 + 14 + nlen].decode("latin1")
            path = f"{prefix}/{name}"
            is_dir = bool(attr & 0x10)
            yield path, s, sz, is_dir
            if is_dir:
                yield from self.entries(s, sz, path)
            if right:
                stack.append(right)
            if left:
                stack.append(left)

    def find(self, disc_path):
        want = "/" + disc_path.replace("\\", "/").strip("/").upper()
        for p, s, sz, d in self.entries():
            if p.upper() == want and not d:
                return s, sz
        return None

    def copy(self, sector, size, out):
        os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
        self.f.seek(self.base + sector * SECTOR)
        with open(out, "wb") as o:
            left = size
            while left:
                chunk = self.f.read(min(left, 1 << 20))
                o.write(chunk)
                left -= len(chunk)


def main(argv):
    if len(argv) < 2:
        raise SystemExit(__doc__)
    cmd, iso = argv[0], Xiso(argv[1])
    if cmd == "list":
        for p, s, sz, d in sorted(iso.entries()):
            print(f"{'<dir>' if d else sz:>12}  {p}")
    elif cmd == "get" and len(argv) == 4:
        hit = iso.find(argv[2])
        if not hit:
            raise SystemExit(f"{argv[2]} not on the disc")
        iso.copy(*hit, argv[3])
        print(f"{argv[2]} -> {argv[3]} ({hit[1]} bytes)")
    elif cmd == "xbes" and len(argv) == 3:
        for p, s, sz, d in iso.entries(iso.root_sector, iso.root_size):
            if not d and p.count("/") == 1 and p.lower().endswith(".xbe"):
                out = os.path.join(argv[2], p[1:].lower())
                iso.copy(s, sz, out)
                print(f"{p} -> {out} ({sz} bytes)")
    elif cmd == "extract" and len(argv) == 3:
        for p, s, sz, d in iso.entries():
            out = os.path.join(argv[2], p.lstrip("/"))
            if d:
                os.makedirs(out, exist_ok=True)
            else:
                iso.copy(s, sz, out)
        print("done")
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
