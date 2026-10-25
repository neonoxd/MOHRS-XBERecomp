"""Print an XBE's certificate, XDK library versions and sections.

  python tools/xbe_info.py assets/xbe/default.xbe
"""
import struct
import sys

ENTRY_RETAIL = 0xA8FC57AB
ENTRY_DEBUG = 0x94859D4B


def info(path):
    d = open(path, "rb").read()
    if d[:4] != b"XBEH":
        raise SystemExit(f"{path}: not an XBE")
    base, = struct.unpack_from("<I", d, 0x104)
    at = lambda va: va - base
    _, img_size, _, timestamp, cert = struct.unpack_from("<IIIII", d, 0x108)
    nsec, sec_hdr, flags, entry = struct.unpack_from("<IIII", d, 0x11C)
    nlib, lib_addr = struct.unpack_from("<II", d, 0x160)

    title_id, = struct.unpack_from("<I", d, at(cert) + 8)
    name = d[at(cert) + 12:at(cert) + 12 + 80].decode("utf-16le").split("\0")[0]
    region, = struct.unpack_from("<I", d, at(cert) + 0xA0)
    disc, version = struct.unpack_from("<II", d, at(cert) + 0xA8)
    print(path)
    print(f"  title    '{name}'  id {title_id:08X}  region {region:#x}  disc {disc}  version {version}")
    print(f"  image    base {base:#x}  size {img_size:#x}  timestamp {timestamp:#x}  init flags {flags:#x}")
    print(f"  entry    {entry ^ ENTRY_RETAIL:#x} (retail key), {entry ^ ENTRY_DEBUG:#x} (debug key)")
    print("  libraries")
    for i in range(nlib):
        o = at(lib_addr) + i * 16
        nm = d[o:o + 8].split(b"\0")[0].decode()
        major, minor, build, lflags = struct.unpack_from("<HHHH", d, o + 8)
        print(f"    {nm:8s} {major}.{minor}.{build}  flags {lflags:#06x}")
    print("  sections")
    for i in range(nsec):
        o = at(sec_hdr) + i * 56
        sflags, va, vsize, raw_off, raw_size, name_addr = struct.unpack_from("<IIIIII", d, o)
        nm = d[at(name_addr):d.index(b"\0", at(name_addr))].decode()
        kind = "exec" if sflags & 4 else ""
        print(f"    {nm:10s} {va:#09x}-{va + vsize:#09x}  raw {raw_size:#08x}  flags {sflags:#04x} {kind}")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    for p in sys.argv[1:]:
        info(p)
