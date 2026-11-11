"""Validate timing hook locations against both locally supplied XBEs (Python + capstone).

  python tools/timing_audit.py

Checks every frame-end caller, not just the in-game path, and the opcodes/registers the timer hooks depend on.
"""
import re
import struct
from pathlib import Path

from disasm import load

ROOT = Path(__file__).resolve().parent.parent


def audit(image, config, frame_end, expected, scan, brain, menus, floor, scripts, entities):
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
        assert hooks.get(va) == (name, name in ("LoadingAnimation", "PopupAnimation")), f"{image}: missing {name} at {va:X}"
        code = bytes.fromhex(code)
        assert data[va - base:va - base + len(code)] == code, f"{image}: unexpected instruction at {va:X}"
    # the animation event scan: cmp ebx, edx; jae wrap at compare + 10, the forward scan right after it
    compare, wrap, forward = scan
    assert data[compare - base:compare - base + 2] == bytes.fromhex("3bda"), f"{image}: expected cmp ebx,edx"
    jae = data[compare - base + 10:compare - base + 12]
    assert jae[0] == 0x73 and compare + 12 + jae[1] == wrap and forward == compare + 12, f"{image}: event scan branch"
    assert data[forward - base:forward - base + 2] == bytes.fromhex("33ed"), f"{image}: expected xor ebp,ebp"
    assert hooks.get(wrap) == ("AnimEventFirstScan", False), f"{image}: missing AnimEventFirstScan at {wrap:X}"
    jump = rf'at = 0x{wrap:X}\ncall = "AnimEventFirstScan"\non_true = 0x{forward:X}'
    assert re.search(jump, text), f"{image}: AnimEventFirstScan must jump to {forward:X}"
    # soldier AI brain: cvttss2si eax,[esp+4] right before the goal tick hook; movss xmm2,[0.01] before the decay
    # hook, in a function that ends in ret 4 (its delta argument)
    ticks, decay = brain
    assert data[ticks - 6 - base:ticks - base] == bytes.fromhex("f30f2c442404"), f"{image}: expected cvttss2si eax"
    assert hooks.get(ticks) == ("BrainTicks", False), f"{image}: missing BrainTicks at {ticks:X}"
    assert data[decay - 8 - base:decay - 4 - base] == bytes.fromhex("f30f1015"), f"{image}: expected movss xmm2"
    constant = struct.unpack_from("<I", data, decay - 4 - base)[0]
    assert struct.unpack_from("<f", data, constant - base)[0] == struct.unpack("<f", struct.pack("<f", 0.01))[0],         f"{image}: threat decay constant"
    assert data[decay - base:decay - base + 3] == bytes.fromhex("0f57c9"), f"{image}: expected xorps xmm1,xmm1"
    assert hooks.get(decay) == ("ThreatDecay", False), f"{image}: missing ThreatDecay at {decay:X}"
    # in-game menu and save/profile popup: dt * 20 in xmm0, cvttss2si eax right before the hook at push eax
    for va, name, convert in menus:
        convert = bytes.fromhex(convert)
        assert data[va - len(convert) - base:va - base] == convert, f"{image}: expected cvttss2si eax before {va:X}"
        assert data[va - base] == 0x50, f"{image}: expected push eax at {va:X}"
        assert hooks.get(va) == (name, False), f"{image}: missing {name} at {va:X}"
    # the player's floor ray: lea eax,[esp+hit+4]; push eax; lea ecx,[esp+segment+8]; push ecx | mov ecx,edi;
    # call [edx+0x24] ... cmp edi,1, with the segment, hit record and flag helpers called just before
    world, retry, (ray, segment, hit), helpers = floor
    assert data[world - base:world - base + 5] == bytes.fromhex("8bcfff5224"), f"{image}: expected the ray cast"
    assert data[retry - base:retry - base + 3] == bytes.fromhex("83ff01"), f"{image}: expected cmp edi,1"
    lea = data[world - 13 - base:world - base]
    assert lea[:3] == bytes.fromhex("8d4424") and lea[3] == hit + 4 and lea[4] == 0x50, f"{image}: hit record offset"
    assert lea[5:8] == bytes.fromhex("8d8c24") and struct.unpack_from("<I", lea, 8)[0] == segment + 8 and         lea[12] == 0x51, f"{image}: ray segment offset"
    # the input the segment is built from: lea eax,[esp+ray] (start, end 0x10 on) shortly before
    assert bytes.fromhex("8d8424") + struct.pack("<I", ray) in data[world - 0xC0 - base:world - base],         f"{image}: ray input offset"
    calls = set()
    for at in range(world - 0x50 - base, retry + 0x80 - base):
        if data[at] == 0xE8:
            calls.add((base + at + 5 + struct.unpack_from("<i", data, at + 1)[0]) & 0xFFFFFFFF)
    assert set(helpers) <= calls, f"{image}: floor ray helpers {[hex(h) for h in set(helpers) - calls]} not called"
    assert hooks.get(world) == ("FloorRayWorld", False), f"{image}: missing FloorRayWorld at {world:X}"
    assert hooks.get(retry) == ("FloorRayRetry", False), f"{image}: missing FloorRayRetry at {retry:X}"
    # the script system's frame update, overridden in src/<game>/hooks.cpp: movss xmm1,[esp+4] (its time), then
    # push 0x100 / call (scripts) and push 0x400 / call (messages)
    update, caller = scripts
    assert data[update - base:update - base + 6] == bytes.fromhex("f30f104c2404"), f"{image}: script update prologue"
    assert data[update + 0x19 - base:update + 0x1F - base] == bytes.fromhex("680001000" "0e8"), f"{image}: script runs"
    assert data[update + 0x23 - base:update + 0x29 - base] == bytes.fromhex("6800040000e8"), f"{image}: messages"
    target = (caller + 5 + struct.unpack_from("<i", data, caller + 1 - base)[0]) & 0xFFFFFFFF
    assert data[caller - base] == 0xE8 and target == update, f"{image}: the gameplay frame must call {update:X}"
    source = (ROOT / "src" / ("game" if image == "default.xbe" else "coop") / "hooks.cpp").read_text()
    assert f"X86_FUNC(sub_{update:08X})" in source and "ScriptFrame" in source, f"{image}: script update override"
    # the entity manager's update, overridden too: movss xmm2,[esp+4]; xorps; sub esp,0x14; ucomiss (zero time)
    manager, manager_call = entities
    assert data[manager - base:manager - base + 15] == bytes.fromhex("f30f105424040f57c983ec140f2ed1"),         f"{image}: entity manager prologue"
    target = (manager_call + 5 + struct.unpack_from("<i", data, manager_call + 1 - base)[0]) & 0xFFFFFFFF
    assert data[manager_call - base] == 0xE8 and target == manager, f"{image}: the gameplay frame must call {manager:X}"
    assert f"X86_FUNC(sub_{manager:08X})" in source and "EntityStep" in source, f"{image}: entity manager override"
    print(f"{image}: 9 frame deltas, both wait compares, 3 timer hooks, 4 cosmetic animation hooks, the animation "
          "event scan, 2 AI brain hooks, 3 menu unit hooks, the floor ray retry, the script update and the entity manager verified")


def audit_loader():
    # quick load: the resource loader worker (0x162E30) takes its slot from [0x399758] into ebx once; LoaderSlot
    # re-reads it at the top of its loop (cmp dword [ebx],0), where its index is at [esp+0x24]
    base, data, _ = load(str(ROOT / "assets/xbe/default.xbe"))
    text = (ROOT / "config/game.toml").read_text()
    assert data[0x162E30 - base:0x162E36 - base] == bytes.fromhex("8b1558973900"), "loader worker prologue"
    assert data[0x162E3A - base:0x162E44 - base] == bytes.fromhex("8b5c241869dbb8000000"), "loader slot index"
    assert data[0x162E70 - base:0x162E73 - base] == bytes.fromhex("833b00"), "loader loop head"
    assert data[0x16314F - base:0x163155 - base] == bytes.fromhex("0f841bfdffff"), "loader loop back edge"
    assert 'at = 0x162E70\ncall = "LoaderSlot"' in text, "missing LoaderSlot"
    print("default.xbe: the loader worker's slot hook verified")


def audit_shake(image, update, offset, callers, ret):
    # the player's view shake update (thiscall, 3 arguments): push ecx; xorps xmm1,xmm1; push ebp; push esi; mov esi,ecx;
    # movss xmm0,[esi+rate] (its first shake slot), overridden by ShakeFrame; called on foot and mounted
    base, data, sections = load(str(ROOT / "assets/xbe" / image))
    code = bytes.fromhex("510f57c955568bf1f30f1086") + struct.pack("<I", offset)
    assert data[update - base:update - base + len(code)] == code, f"{image}: shake update prologue"
    assert data[ret - base:ret - base + 3] == bytes.fromhex("c20c00"), f"{image}: shake update ret 0xc at {ret:X}"
    found = set()
    for _, va, size, flags in sections:
        if not flags & 4:
            continue
        for off in range(va - base, va - base + size - 5):
            if data[off] == 0xE8 and (base + off + 5 + struct.unpack_from("<i", data, off + 1)[0]) & 0xFFFFFFFF == update:
                found.add(base + off)
    assert found == set(callers), f"{image}: shake update callers {sorted(hex(c) for c in found)}"
    source = (ROOT / "src" / ("game" if image == "default.xbe" else "coop") / "hooks.cpp").read_text()
    assert f"X86_FUNC(sub_{update:08X})" in source and "ShakeFrame" in source, f"{image}: shake update override"
    print(f"{image}: the view shake update and its {len(callers)} callers verified")


def audit_kick(image, config, block, end, steps):
    # a mounted gun's firing kick in the player update: test byte [esi+0xd46],8 at its start (MountedKick jumps past it
    # to end), each oscillator step a mulss xmm0,[ebp+8] right before its hook, with the step where the hook reads it
    base, data, _ = load(str(ROOT / "assets/xbe" / image))
    text = (ROOT / "config" / config).read_text()
    assert data[block - base:block - base + 7] == bytes.fromhex("f68646" "0d000008"), f"{image}: kick block start"
    assert f'at = 0x{block:X}\ncall = "MountedKick"\non_true = 0x{end:X}' in text, f"{image}: MountedKick hook"
    for at, call, before in steps:
        code = bytes.fromhex(before + "f30f594508")
        assert data[at - len(code) - base:at - base] == code, f"{image}: oscillator step before {at:X}"
        assert f'at = 0x{at:X}\ncall = "{call}"' in text, f"{image}: {call} hook"
    print(f"{image}: the mounted gun kick block and its {len(steps)} oscillator steps verified")


def audit_refire(image, config, update, timer, add):
    # a weapon's state update (thiscall (time, flag), ret 8; push ebp; mov ebp,esp; ...; mov esi,ecx; mov cl,[esi+flags]),
    # overridden for its time; in the fire routine, cvtsi2ss xmm0,ecx then addss xmm0,[esi+timer], where RefireTime hooks
    base, data, _ = load(str(ROOT / "assets/xbe" / image))
    text = (ROOT / "config" / config).read_text()
    prologue = bytes.fromhex("558bec83e4f081ecf400000053568bf18a8e")
    assert data[update - base:update - base + len(prologue)] == prologue, f"{image}: weapon update prologue"
    code = bytes.fromhex("f30f2ac1f30f5886") + struct.pack("<I", timer)
    assert data[add - 4 - base:add - 4 - base + len(code)] == code, f"{image}: refire add at {add:X}"
    assert f'at = 0x{add:X}\ncall = "RefireTime"' in text, f"{image}: RefireTime hook"
    source = (ROOT / "src" / ("game" if image == "default.xbe" else "coop") / "hooks.cpp").read_text()
    assert f"X86_FUNC(sub_{update:08X})" in source and "WeaponUpdate(ctx)" in source, f"{image}: weapon update override"
    print(f"{image}: the weapon update and its refire add verified")


if __name__ == "__main__":
    audit_loader()
    audit_refire("default.xbe", "game.toml", 0x151320, 0x300, 0x151234)
    audit_refire("moh3rdc.xbe", "coop.toml", 0x14A6A0, 0x310, 0x14A5B2)
    audit_shake("default.xbe", 0xD6700, 0xD78, (0xDC90C, 0xE0AE9), 0xD6A49)
    audit_kick("default.xbe", "game.toml", 0xDDA3F, 0xDDC23, [(0xDDB92, "MountedKickStep", "0f28c3")])
    audit_kick("moh3rdc.xbe", "coop.toml", 0xD75F5, 0xD7848,
               [(0xD774D, "MountedKickSeatStep", "f30f10867c0e0000"), (0xD77BC, "MountedKickStep", "0f28c2")])
    audit_shake("moh3rdc.xbe", 0xCB440, 0xD90, (0xD614C, 0xD8A19), 0xCB789)
    audit("default.xbe", "game.toml", 0x13A90, [
        (0x13AAF, "FrameWaitEax", "83f802"), (0x13ACA, "FrameWaitEcx", "83f902"),
        (0xA4C32, "MissionTicks", "506a00"), (0xA4CEC, "MenuTicks", "50"),
        (0xFABB4, "MessageTicks", "f30f5cc3"),
        (0xFC9FD, "ResetLoadingAnimation", "895e10"),
        (0xFD3DC, "LoadingAnimation", "f30f2ac04083f821"),
        (0xF6B74, "ResetPopupAnimation", "c7461400000000"),
        (0xF6C5A, "PopupAnimation", "f30f2ac04083f821"),
    ], (0x6FB01, 0x6FB74, 0x6FB0D), (0x197C6, 0x223A8),
        [(0x11DDCB, "MenuTicks", "f30f2c44240c"), (0x11DC48, "MenuTicks", "f30f2c442428"), (0xF68D6, "PopupTicks", "f30f2cc0")],
        (0xDAC19, 0xDAC2A, (0x80, 0xC0, 0x40), (0x144C30, 0x142910, 0x142AB0, 0x7A8A0)), (0x4C0F0, 0xA4B52), (0xFAB00, 0xA4C7B))
    audit("moh3rdc.xbe", "coop.toml", 0x13AE0, [
        (0x13AFF, "FrameWaitEax", "83f802"), (0x13B1A, "FrameWaitEcx", "83f902"),
        (0x98C88, "MissionTicks", "506a00"), (0x98D94, "MenuTicks", "50"),
        (0xF3684, "MessageTicks", "f30f5cc3"),
        (0xF550D, "ResetLoadingAnimation", "895e10"),
        (0xF5EEC, "LoadingAnimation", "f30f2ac04083f821"),
        (0xEF594, "ResetPopupAnimation", "c7461400000000"),
        (0xEF67A, "PopupAnimation", "f30f2ac04083f821"),
    ], (0x62541, 0x625B4, 0x6254D), (0x19056, 0x21F58),
        [(0x11823D, "MenuTicks", "f30f2c442410"), (0x118078, "MenuTicks", "f30f2c442428"), (0xEF2F6, "PopupTicks", "f30f2cc0")],
        (0xD1581, 0xD1592, (0x90, 0xE0, 0x50), (0x13DF00, 0x13BBE0, 0x13BD80, 0x6D290)), (0x4F220, 0x98B07), (0xF35D0, 0x98CB9))
