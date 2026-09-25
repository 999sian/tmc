#!/usr/bin/env python3
"""Check Picori's compiled pickup keys against the USA game ROM.

Usage: python3 tools/verify_picori_rules.py build/pc/baserom.gba
"""

import re
import struct
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
rom = Path(sys.argv[1]).read_bytes()
native_source = (ROOT / "port/rando/picori_rules.hpp").read_text().split(
    "static const NativeLocationSpec kNativeLocations[] = {", 1
)[1].split("\n};", 1)[0]
native_rows = re.findall(
    r'^\s*\{\s*"([^"]+)",\s*(RANDO_LOGIC_LOCATION_[A-Z_]+),\s*'
    r'(UINT32_MAX|0x[0-9A-Fa-f]+u),\s*'
    r'(?:"[^"]*"|nullptr),\s*(?:"[^"]*"|nullptr),\s*'
    r'(?:"[^"]*"|nullptr),\s*(NATIVE_[A-Z_]+)\s*\},\s*$',
    native_source, re.MULTILINE,
)
assert len(native_rows) == len(re.findall(r'^\s*\{', native_source, re.MULTILINE)), \
    "unrecognized compiled location row"
assert {row[3] for row in native_rows} <= {
    "NATIVE_ALWAYS", "NATIVE_START_SWORD", "NATIVE_NO_START_SWORD",
    "NATIVE_DOJO_SHUFFLED", "NATIVE_DOJO_ORIGINAL", "NATIVE_POOL_LEAN",
    "NATIVE_POOL_NORMAL", "NATIVE_POOL_PLENTIFUL", "NATIVE_RUPEEMANIA",
}, "new location condition needs a ROM verification profile"


def word(pos):
    return struct.unpack_from("<I", rom, pos)[0]


def offset(pos):
    return pos - 0x08000000 if 0x08000000 <= pos < 0x08000000 + len(rom) else None


metadata = (ROOT / "src/data/areaMetadata.c").read_text().split(
    "const AreaHeader gAreaMetadata[] = {", 1
)[1].split("};", 1)[0]
banks = []
for row in re.findall(r"\{([^{}]*)\}", metadata):
    bank = row.split(",")[2].strip()
    match = re.fullmatch(r"LOCAL_BANK_(\d+|G)", bank)
    banks.append(int(match[1]) if match and match[1] != "G" else 0)

tables = [offset(word(0xD50FC + area * 4)) for area in range(0x90)]
starts = sorted({base for base in tables if base is not None})
ends = {base: starts[i + 1] if i + 1 < len(starts) else 0xD50FC
        for i, base in enumerate(starts)}
chests = {}
ground = {}
for area, base in enumerate(tables):
    if base is None:
        continue
    for room in range(min(64, (ends[base] - base) // 4)):
        props = offset(word(base + room * 4))
        if props is None:
            continue
        tiles = offset(word(props + 12))
        if tiles is not None:
            ordinal = 0
            for i in range(256):
                kind, flag = struct.unpack_from("<BB", rom, tiles + i * 8)
                if kind == 0:
                    break
                if kind in (2, 3):
                    chests[(area, room, ordinal)] = (banks[area], flag)
                    ordinal += 1
            else:
                raise ValueError(f"unterminated chest list {area:02X}-{room:02X}")
        for prop in range(3):
            entities = offset(word(props + prop * 4))
            if entities is None:
                continue
            for i in range(512):
                kind, _, eid, _, _, _, _, sprite = struct.unpack_from(
                    "<BBBBIHHI", rom, entities + i * 16
                )
                if kind == 0xFF:
                    break
                flag = (sprite >> 16) & 0xFF
                if kind & 15 == 6 and eid == 0 and flag:
                    ground[(area, room, flag)] = (banks[area], flag)
            else:
                raise ValueError(f"unterminated entity list {area:02X}-{room:02X}")

script_source = (ROOT / "port/rando/rando_keymap.c").read_text()
script_names = set(re.findall(
    r'\{\s*"([^"]+)"\s*,\s*RANDO_SCRIPTED_KEY',
    script_source.split("static const RandoScriptedKeyEntry kScriptedKeys[] = {", 1)[1],
))
ground_source = script_source.split(
    "static const RandoKeymapEntry kGroundKeys[] = {", 1
)[1].split("\n};", 1)[0]
ground_aliases = {}
for name, area, room, flag, _regional_flag in re.findall(
    r'\{\s*"([^"]+)"\s*,\s*0x([0-9A-Fa-f]+)\s*,\s*0x([0-9A-Fa-f]+)\s*,\s*0x([0-9A-Fa-f]+)(?:\s*,\s*0x([0-9A-Fa-f]+))?\s*\}',
    ground_source,
):
    ground_aliases[name] = (int(area, 16), int(room, 16), int(flag, 16))
bound_aliases = script_names | set(ground_aliases)

def verify(conditions):
    seen = {}
    direct = 0
    shuffled = 0
    for name, kind, key_text, condition in native_rows:
        if condition not in conditions or kind == "RANDO_LOGIC_LOCATION_HELPER":
            continue
        if kind in ("RANDO_LOGIC_LOCATION_ANY", "RANDO_LOGIC_LOCATION_MAJOR",
                    "RANDO_LOGIC_LOCATION_MINOR", "RANDO_LOGIC_LOCATION_DUNGEON",
                    "RANDO_LOGIC_LOCATION_DUNGEON_PRIZE"):
            shuffled += 1
        if name.startswith(("Chest_", "Ground_")):
            match = re.fullmatch(r"(?:Chest|Ground)_([0-9A-F]{2})_([0-9A-F]{2})_([0-9A-F]{2})", name)
            assert match, name
            key = tuple(int(part, 16) for part in match.groups())
            assert key_text == f"0x{key[0]:02X}{key[1]:02X}{key[2]:02X}u", name
            physical = (chests if name.startswith("Chest_") else ground).get(key)
            assert physical is not None, f"no ROM pickup for {name}"
            direct += 1
        elif name not in {"StartSword", "StartKinstoneBag", "Goal"}:
            assert name in bound_aliases, f"no native key binding for {name}"
            assert key_text == "UINT32_MAX", name
            physical = None
            if name in ground_aliases:
                area, _room, flag = ground_aliases[name]
                physical = (banks[area], flag)
        else:
            physical = None
        if physical is not None:
            assert physical not in seen, f"same persistent check: {seen[physical]} and {name}"
            seen[physical] = name
    return shuffled, direct, sum(name in ground_aliases for name in seen.values())


baseline = {"NATIVE_ALWAYS", "NATIVE_START_SWORD", "NATIVE_DOJO_SHUFFLED", "NATIVE_POOL_NORMAL"}
shuffled, direct, aliases = verify(baseline)
vanilla_shuffled, _, _ = verify((baseline - {"NATIVE_DOJO_SHUFFLED"}) | {"NATIVE_DOJO_ORIGINAL"})
obscure_shuffled, obscure_direct, _ = verify(baseline | {"NATIVE_RUPEEMANIA"})
assert shuffled >= 259, f"only {shuffled} shuffled default checks"
assert vanilla_shuffled >= 259, f"only {vanilla_shuffled} shuffled checks with vanilla dojos"
assert obscure_shuffled > shuffled
print(f"Picori rules: {shuffled} default / {vanilla_shuffled} vanilla dojo checks; "
      f"{direct} direct ROM pickups + {aliases} ground aliases verified "
      f"({obscure_direct} direct with extra checks)")
