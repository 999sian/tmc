#!/usr/bin/env python3
"""Check Picori's direct pickup keys against the USA game ROM.

Usage: python3 tools/verify_picori_rules.py build/pc/baserom.gba
"""

import re
import struct
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
rom = Path(sys.argv[1]).read_bytes()
logic = (ROOT / "assets/rando/picori.logic").read_text().splitlines()


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

def verify(defines):
    seen = {}
    direct = 0
    active = True
    stack = []
    shuffled = 0
    for source_line in logic:
        line = source_line.split("#", 1)[0].strip()
        if line.startswith("!ifdef - ") or line.startswith("!ifndef - "):
            name = line.split("-", 1)[1].strip()
            cond = name in defines
            if line.startswith("!ifndef"):
                cond = not cond
            stack.append((active, cond))
            active = active and cond
            continue
        if line == "!else":
            parent, cond = stack[-1]
            active = parent and not cond
            stack[-1] = (parent, not cond)
            continue
        if line == "!endif":
            active = stack.pop()[0]
            continue
        if not active or not line or line.startswith("!") or line.startswith("Items."):
            continue
        fields = [part.strip() for part in line.split(";")]
        if len(fields) < 2 or fields[1] == "Helper":
            continue
        name = fields[0].split(":", 1)[0]
        if fields[1] in ("Any", "Major", "Minor", "Dungeon", "DungeonPrize"):
            shuffled += 1
        if name.startswith(("Chest_", "Ground_")):
            match = re.fullmatch(r"(?:Chest|Ground)_([0-9A-F]{2})_([0-9A-F]{2})_([0-9A-F]{2})", name)
            assert match, name
            key = tuple(int(part, 16) for part in match.groups())
            assert fields[2].upper() == "-".join(f"{part:02X}" for part in key), name
            physical = (chests if name.startswith("Chest_") else ground).get(key)
            assert physical is not None, f"no ROM pickup for {name}"
            direct += 1
        elif name not in {"StartSword", "StartKinstoneBag", "Goal"}:
            assert name in bound_aliases, f"no native key binding for {name}"
            physical = None
            if name in ground_aliases:
                area, _room, flag = ground_aliases[name]
                physical = (banks[area], flag)
        else:
            physical = None
        if physical is not None:
            assert physical not in seen, f"same persistent check: {seen[physical]} and {name}"
            seen[physical] = name
    assert not stack, "unclosed conditional"
    return shuffled, direct, sum(name in ground_aliases for name in seen.values())


baseline = {"START_SMITH_SWORD", "DOJOANY", "ITEM_POOL_NORMAL", "ACCESS_BEATABLE"}
shuffled, direct, aliases = verify(baseline)
vanilla_shuffled, _, _ = verify(baseline - {"DOJOANY"})
obscure_shuffled, obscure_direct, _ = verify(baseline | {"RUPEEMANIA"})
assert shuffled >= 259, f"only {shuffled} shuffled default checks"
assert vanilla_shuffled >= 259, f"only {vanilla_shuffled} shuffled checks with vanilla dojos"
assert obscure_shuffled > shuffled
print(f"Picori rules: {shuffled} default / {vanilla_shuffled} vanilla dojo checks; "
      f"{direct} direct ROM pickups + {aliases} ground aliases verified "
      f"({obscure_direct} direct with extra checks)")
