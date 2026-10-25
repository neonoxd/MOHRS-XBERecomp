"""Validate timing hook locations against both locally supplied XBEs (Python + capstone).

  python tools/timing_audit.py

Checks every frame-end caller, not just the in-game path, and the opcodes/registers the timer hooks depend on.
"""
import re
import struct
from pathlib import Path

from disasm import load

ROOT = Path(__file__).resolve().parent.parent


def audit(image, config, frame_end, expected):
    base, data, sections = load(str(ROOT / "assets/xbe" / image))
    text = (ROOT / "config" / config).read_text()
    hooks = {}
    for block in text.split("[[hook]]")[1:]:
        address = re.search(r"^at = (0x[\dA-Fa-f]+)", block, re.M)
        call = re.search(r'^call = "(\w+)"', block, re.M)
        if address and call:
            hooks[int(address[1], 16)] = (call[1], bool(re.search(r"^after = true", block, re.M)))
    callers = set()
    for _, va, size, flags in sections:
        if not flags & 4:
            continue
        for offset in range(va - base, va - base + size - 5):
            if data[offset] == 0xE8:
                target = (base + offset + 5 + struct.unpack_from("<i", data, offset + 1)[0]) & 0xFFFFFFFF
                if target == frame_end:
                    callers.add(base + offset + 5)
    delta_hooks = {va for va, hook in hooks.items() if hook == ("FrameDelta", True)}
    assert len(callers) == 9 and delta_hooks == callers, f"{image}: frame-end callers not all hooked"
    for va in callers:
        assert data[va - base:va - base + 4] == bytes.fromhex("f30f2ac0"), f"{va:X}: expected cvtsi2ss xmm0,eax"
    for va, name, code in expected:
        assert hooks.get(va) == (name, False), f"{image}: missing {name} at {va:X}"
        code = bytes.fromhex(code)
        assert data[va - base:va - base + len(code)] == code, f"{image}: unexpected instruction at {va:X}"
    print(f"{image}: 9 frame deltas, both wait compares and 3 timer hooks verified")


if __name__ == "__main__":
    audit("default.xbe", "game.toml", 0x13A90, [
        (0x13AAF, "FrameWaitEax", "83f802"), (0x13ACA, "FrameWaitEcx", "83f902"),
        (0xA4C32, "MissionTicks", "506a00"), (0xA4CEC, "MenuTicks", "50"),
        (0xFABB4, "MessageTicks", "f30f5cc3"),
    ])
    audit("moh3rdc.xbe", "coop.toml", 0x13AE0, [
        (0x13AFF, "FrameWaitEax", "83f802"), (0x13B1A, "FrameWaitEcx", "83f902"),
        (0x98C88, "MissionTicks", "506a00"), (0x98D94, "MenuTicks", "50"),
        (0xF3684, "MessageTicks", "f30f5cc3"),
    ])
