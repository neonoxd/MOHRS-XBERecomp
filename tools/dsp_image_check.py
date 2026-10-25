"""Which effects of a DSP effects image the runtime reproduces: decrypts each effect's code the way
sdk/runtime/xdk/dsp_image.cpp does and looks its hash up in the same table.

  python tools/dsp_image_check.py assets/xbe/default.xbe 0x1F2F20 0x3360 [sdk/xdk/5455/layout.toml]
  python tools/dsp_image_check.py dsstdfx.bin

The image is read from an XBE at a virtual address and size, or from a file. The descriptor table is found with the
EffectsImage_* keys of the layout (5849's values when none is given). Keep MasterKey, Decrypt, CodeHash and KINDS
in step with dsp_image.cpp.
"""
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from disasm import load  # noqa: E402

M32 = 0xFFFFFFFF
KINDS = {
    0xDDD7C789A714E88B: "gain",
    0xFA00AFA73549BF9B: "mixer",
    0xA8ACC4A41FE77751: "filter",
    0x13CF05D0E014E22E: "reverb",
    0xF1E0A321578CFED9: "crosstalk",
}


def step(lo, hi):
    taps = (lo ^ (lo << 1) ^ (lo << 3)) & M32
    top = ((taps << 28) & M32) ^ hi
    return ((lo >> 1) | (hi << 31)) & M32, (top & 0x80000000) | (hi >> 1)


def master_key():
    lo, hi = 0x49DE12BA, 0x7FA49BCA
    for _ in range(81):
        lo, hi = step(lo, hi)
    return struct.pack("<II", lo, hi)


def decrypt(key, data):
    m = master_key()
    lo, hi = struct.unpack("<II", m)
    lo, hi = step(lo, hi)
    out = bytearray(len(data))
    for i, b in enumerate(data):
        mask = m[i & 7]
        c = (b - key[i & 7] * mask) & 0xFF
        lo, hi = step(lo, hi)
        out[i] = mask ^ (lo & 0xFF) ^ c
    return bytes(out)


def code_hash(code):
    h = 0xCBF29CE484222325
    for i, b in enumerate(code):
        h = ((h ^ (0 if 4 <= i < 8 else b)) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def main(argv):
    if not argv:
        raise SystemExit(__doc__)
    if len(argv) >= 3 and argv[1].startswith("0x"):
        base, img, _ = load(argv[0])
        va, size = int(argv[1], 16), int(argv[2], 16)
        image = img[va - base:va - base + size]
        rest = argv[3:]
    else:
        image, rest = Path(argv[0]).read_bytes(), argv[1:]
    keys = {"EffectsImage_Skip0": 0x804, "EffectsImage_Skip1": 0x80C, "EffectsImage_Desc": 0x818}
    if rest:
        for m in re.finditer(r"^(EffectsImage_\w+)\s*=\s*(0x[0-9A-Fa-f]+)", Path(rest[0]).read_text(), re.M):
            keys[m.group(1)] = int(m.group(2), 16)
    u32 = lambda o: struct.unpack_from("<I", image, o)[0] if o + 4 <= len(image) else 0
    table = keys["EffectsImage_Desc"] + (u32(keys["EffectsImage_Skip0"]) + u32(keys["EffectsImage_Skip1"])) * 4
    count = u32(table)
    print(f"image {len(image):#x} bytes, descriptor at {table:#x}, {count} effects")
    if not 0 < count <= 64:
        raise SystemExit("descriptor count out of range: wrong layout keys or not an effects image")
    keys_at = table + 8 + count * 32
    effect_keys = decrypt(master_key(), image[keys_at:keys_at + count * 8])
    for i in range(count):
        entry = table + 8 + i * 32
        code_at, code_bytes, state_at, state_bytes = struct.unpack_from("<IIII", image, entry)
        code = decrypt(effect_keys[i * 8:i * 8 + 8], image[code_at:code_at + code_bytes])
        h = code_hash(code)
        print(f"  {i:2d} code {code_at:#06x} {code_bytes // 4:5d} words  state {state_bytes // 4:4d} words  "
              f"hash {h:#018x}  {KINDS.get(h, 'NOT RECOGNISED')}")


if __name__ == "__main__":
    main(sys.argv[1:])
