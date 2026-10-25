"""Checks the runtime's HLE declarations against the image: for every host function a config names, the stack bytes
the guest function pops (its rets, following branches and tail jumps) against what the declaration pops.

  python tools/hle_audit.py assets/xbe/default.xbe config/game.toml [sdk/runtime ...]

HLE_STDCALL(name, n) pops 4n, HLE_FASTCALL(name, n) pops 4n after ecx and edx, HLE_CDECL pops nothing; HLE_WRAP runs
before the guest body and is listed for completeness. Needs capstone (pip install capstone).
"""
import re
import sys
from pathlib import Path

import capstone

sys.path.insert(0, str(Path(__file__).parent))
from disasm import load  # noqa: E402

DECL = re.compile(r"\b(HLE_STDCALL|HLE_FASTCALL|HLE_WRAP)\((\w+),\s*(\d+)\)|\b(HLE_CDECL)\((\w+)\)")
ENTRY = re.compile(r"^(\w+)\s*=\s*(0x[0-9A-Fa-f]+)\b")


# a macro that declares one: #define BUFFER(name, nargs) ... HLE_STDCALL(IDirectSoundBuffer_##name, nargs)
MACRO = re.compile(r"#define (\w+)\((\w+)(?:,\s*(\w+))?[^)]*\)[^\n]*(?:\\\n[^\n]*)*?"
                   r"(HLE_STDCALL|HLE_FASTCALL|HLE_WRAP)\((\w*)##\2,\s*(\w+)\)")


def declarations(dirs):
    out = {}
    for d in dirs:
        for p in sorted(Path(d).rglob("*.cpp")):
            text = p.read_text(errors="replace")
            where = f"{p.relative_to(Path(d).parent)}"
            for m in DECL.finditer(text):
                kind, name, n = (m.group(1), m.group(2), int(m.group(3))) if m.group(1) else (m.group(4), m.group(5), 0)
                out[name] = (kind, n, where)
            for m in MACRO.finditer(text):
                macro, count_param, kind, prefix, count = m.group(1), m.group(3), m.group(4), m.group(5), m.group(6)
                for use in re.finditer(rf"^{macro}\((\w+)(?:,\s*([^)]+))?\)", text, re.M):
                    args = [a.strip() for a in (use.group(2) or "").split(",")]
                    n = int(args[0]) if count == count_param and args and args[0].isdigit() else int(count) if count.isdigit() else None
                    if n is not None:
                        out[prefix + use.group(1)] = (kind, n, where + f" via {macro}")
    return out


def pops(img, base, va, md, depth=0):
    """set of ret immediates reachable from va; tail jumps outside the function are followed once."""
    seen, work, rets = set(), [va], set()
    while work:
        p = work.pop()
        while p not in seen:
            seen.add(p)
            ins = next(md.disasm(img[p - base:p - base + 15], p, 1), None)
            if not ins:
                break
            m = ins.mnemonic
            if m == "ret":
                rets.add(int(ins.op_str, 16) if ins.op_str else 0)
                break
            if m == "int3":
                break
            target = None
            if (m == "jmp" or (m.startswith("j") and m != "jmp")) and ins.op_str.startswith("0x"):
                target = int(ins.op_str, 16)
            if m == "jmp":
                if target is None:
                    break
                # a far jump is a tail call into another function
                if abs(target - va) > 0x4000 and depth < 2:
                    rets |= pops(img, base, target, md, depth + 1)
                    break
                p = target
                continue
            if target is not None:
                work.append(target)
            p += ins.size
    return rets


def main(argv):
    if len(argv) < 2:
        raise SystemExit(__doc__)
    base, img, _ = load(argv[0])
    cfg = {m.group(1): int(m.group(2), 16) for line in open(argv[1]) if (m := ENTRY.match(line))}
    decl = declarations(argv[2:] or ["sdk/runtime"])
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    bad = 0
    for name, (kind, n, where) in sorted(decl.items()):
        va = cfg.get(name)
        if not va:
            continue
        expect = {"HLE_STDCALL": 4 * n, "HLE_FASTCALL": 4 * n, "HLE_CDECL": 0, "HLE_WRAP": 4 * n}[kind]
        got = pops(img, base, va, md)
        ok = got == {expect}
        bad += not ok
        print(f"{'ok ' if ok else 'BAD'} {name:44s} {va:#08x} {kind}({n}) pops {expect}, image pops "
              f"{sorted(got) if got else 'nothing found'}  [{where}]")
    print(f"{bad} mismatches")


if __name__ == "__main__":
    main(sys.argv[1:])
