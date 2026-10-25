"""Compare config [symbols]/[data] with another symbol list, such as XbSymbolDatabaseCLI's output.

  python tools/symbols_compare.py config/game.toml re/xbsdb_default.txt [--apply]

The other list holds "LIB__Name = 0xADDR" lines. Names are matched after dropping the library prefix. Prints the
names both have (agree or conflict), the config names at 0 the other list knows, and the ones still missing.
--apply writes the newly found addresses into the config, each with a trailing "# from <list>" comment so they stay
marked as unverified until checked by hand.
"""
import re
import sys
from pathlib import Path

LINE = re.compile(r"^(\w+?)__(\w+) = 0x([0-9a-fA-F]+)$")
ENTRY = re.compile(r"^(\w+)(\s*=\s*)(0x[0-9A-Fa-f]+|0)\b(.*)$")

# config name -> other list's (XbSymbolDatabase's) name, where they differ and mean the same thing. Its _<n> suffix
# marks a variant popping n stack bytes; only aliased where the reference signature pops the same.
ALIASES = {
    "D3D_pDevice": "D3D_g_pDevice",
    "D3D_RenderState": "D3D_g_RenderState",
    "D3D_DeferredTextureState": "D3D_g_DeferredTextureState",
    "D3D_Streams": "D3D_g_Stream",
    "D3D_MakeRequestedSpace": "D3D_MakeRequestedSpace_8",
    "IDirectSoundBuffer_SetMixBinVolumes": "IDirectSoundBuffer_SetMixBinVolumes_8",
    "XPP_GamepadType": "g_DeviceType_Gamepad",
    "XPP_MemoryUnitType": "g_DeviceType_MU",
}


def config_tables(path):
    """name -> address per table, for the plain "name = 0x..." lines xbe_symbols writes (no tomllib on 3.10)."""
    tables, table = {}, None
    for line in Path(path).read_text().splitlines():
        if line.startswith("["):
            table = line.strip("[] ")
            continue
        m = ENTRY.match(line)
        if table and m:
            tables.setdefault(table, {})[m.group(1)] = int(m.group(3), 16) if m.group(3) != "0" else 0
    return tables


def other_list(path):
    out = {}
    for line in Path(path).read_text().splitlines():
        m = LINE.match(line.strip())
        if m:
            out.setdefault(m.group(2), int(m.group(3), 16))
    return out


def main(argv):
    if len(argv) < 2:
        raise SystemExit(__doc__)
    cfg_path, other_path, apply = Path(argv[0]), argv[1], "--apply" in argv
    cfg = config_tables(cfg_path)
    other = other_list(other_path)
    agree, conflict, filled, missing = [], [], {}, []
    for table in ("symbols", "data"):
        for name, va in cfg.get(table, {}).items():
            o = other.get(ALIASES.get(name, name))
            if va and o is not None:
                (agree if o == va else conflict).append((name, va, o))
            elif not va and o is not None:
                filled[name] = o
            elif not va:
                missing.append(name)
    print(f"agree {len(agree)}, conflict {len(conflict)}, newly found {len(filled)}, still missing {len(missing)}")
    for n, va, o in conflict:
        print(f"  CONFLICT {n}: config {va:#x}, {Path(other_path).name} {o:#x}")
    for n, o in sorted(filled.items()):
        print(f"  found    {n} = {o:#x}")
    print("  missing  " + " ".join(sorted(missing)))
    if apply and filled:
        lines = cfg_path.read_text().splitlines(keepends=True)
        for i, line in enumerate(lines):
            m = ENTRY.match(line.rstrip("\r\n"))
            if m and m.group(1) in filled and m.group(3) == "0":
                end = "\n" if line.endswith("\n") else ""
                lines[i] = f"{m.group(1)}{m.group(2)}{filled[m.group(1)]:#08X}  # from {Path(other_path).name}{end}"
                lines[i] = lines[i].replace("0X", "0x")
        cfg_path.write_text("".join(lines))
        print(f"wrote {len(filled)} addresses into {cfg_path}")


if __name__ == "__main__":
    main(sys.argv[1:])
