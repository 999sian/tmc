#!/usr/bin/env python3
"""Compile the production additional-list initializer against synthetic regional ROMs.

Optionally validate retail data with --usa, --eu and --jp ROM paths. ROM bytes
are never included in the repository. Region offsets were verified by matching
all 12 non-pointer bytes of every entity and each list's final terminator.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
# name, USA, EU, JP, copied capacity (including the 16-byte terminator)
LISTS = [
    ("gUnk_additional_a_DeepwoodShrineBoss_Main", 0xDF94C, 0xDF068, 0xDF6BC, 0x30),
    ("gUnk_additional_8_MelarisMine_Main", 0xDD214, 0xDC950, 0xDCFA4, 0x60),
    ("gUnk_additional_9_MelarisMine_Main", 0xDD274, 0xDC9B0, 0xDD004, 0x20),
    ("gUnk_additional_8_DeepwoodShrine_StairsToB1", 0xDE834, 0xDDF70, 0xDE5C4, 0x30),
    ("gUnk_additional_8_HouseInteriors1_Library1F", 0xD66F4, 0xD5E50, 0xD6494, 0x20),
    ("gUnk_additional_9_HouseInteriors1_Library1F", 0xD6734, 0xD5E90, 0xD64D4, 0x50),
    ("gUnk_additional_8_HyruleCastle_3", 0xD7690, 0xD6DEC, 0xD7430, 0x40),
    ("gUnk_additional_a_CaveOfFlamesBoss_Main", 0xE1814, 0xE0F30, 0xE1584, 0x30),
    ("gUnk_additional_a_TempleOfDroplets_BigOcto", 0xE49F4, 0xE40F0, 0xE4744, 0x30),
    ("gUnk_additional_8_PalaceOfWinds_GyorgTornado", 0xE72E4, 0xE69E0, 0xE7034, 0x30),
    ("gUnk_additional_9_PalaceOfWinds_GyorgTornado", 0xE7314, 0xE6A10, 0xE7064, 0x30),
    ("gUnk_additional_c_HouseInteriors2_Romio", 0xF236C, 0xF19A0, 0xF2094, 0x20),
    ("gUnk_additional_9_HouseInteriors2_Percy", 0xF2718, 0xF1D4C, 0xF2440, 0x40),
    ("gUnk_additional_a_HouseInteriors2_Percy", 0xF2758, 0xF1D8C, 0xF2480, 0x40),
    ("gUnk_additional_8_HouseInteriors3_BorlovEntrance", 0xF5F38, 0xF54F4, 0xF5C50, 0x20),
    ("gUnk_additional_9_HouseInteriors3_BorlovEntrance", 0xF5F58, 0xF5514, 0xF5C70, 0x20),
    ("gUnk_additional_a_HouseInteriors3_BorlovEntrance", 0xF5F78, 0xF5534, 0xF5C90, 0x20),
    ("Entities_MinishPaths_MayorsCabin_gUnk_080D6138", 0xD6138, 0xD5894, 0xD5ED8, 0x60),
    ("Entities_HouseInteriors1_Mayor_080D6210", 0xD6210, 0xD596C, 0xD5FB0, 0x50),
    ("UpperInn_Oracles", 0xD6BF4, 0xD6350, 0xD6994, 0x40),
    ("UpperInn_NoFarore", 0xD6C34, 0xD6390, 0xD69D4, 0x30),
    ("UpperInn_NoDin", 0xD6C64, 0xD63C0, 0xD6A04, 0x30),
    ("UpperInn_NoNayru", 0xD6C94, 0xD63F0, 0xD6A34, 0x30),
    ("UpperInn_Din", 0xD6CC4, 0xD6420, 0xD6A64, 0x20),
    ("UpperInn_Nayru", 0xD6CE4, 0xD6440, 0xD6A84, 0x20),
    ("UpperInn_Farore", 0xD6D04, 0xD6460, 0xD6AA4, 0x20),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for region in ('usa', 'eu', 'jp'):
        parser.add_argument('--' + region, type=Path)
    args = parser.parse_args()
    source = (ROOT / 'port/data_stubs_autogen.c').read_text()
    start = source.index('        extern EntityData gUnk_additional_a_DeepwoodShrineBoss_Main;')
    end = source.index('    fprintf(stderr, "Port_InitDataStubs:', start)
    block = source[start:end].rsplit('    }', 1)[0]
    block = re.sub(r'        extern EntityData \w+;\n', '', block)
    header = (ROOT / 'port/port_rom.h').read_text()
    resolver = header[header.index('static inline void* Port_ResolveRomData('):header.index('/**\n * Read entry [idx]')]
    helpers = ''
    if '/* Copied ROM entity-list provenance. */' in source:
        helpers = source[source.index('/* Copied ROM entity-list provenance. */'):source.index('void Port_InitDataStubs(void)')]
    capacities = dict(re.findall(r'ENTITY_DATA_STUB\((\w+), (\d+)\)',
                                (ROOT / 'port/port_linked_stubs.c').read_text()))
    declarations = []
    checks = []
    fills = []
    for number, (name, usa, eu, jp, size) in enumerate(LISTS):
        assert size <= int(capacities[name]), name
        declarations.append(f'EntityData {name}[{int(capacities[name]) // 16}];')
        offsets = (usa, eu, jp)
        for region, offset in enumerate(offsets, 1):
            fills.append(f'if (gRomRegion == {region}) {{ memset(gRomData+{offset}, {number+1}, {size}); gRomData[{offset+size-16}] = 255; }}')
            checks.append(f'if (gRomRegion == {region}) assert(memcmp(&{name}, gRomData+{offset}, {size}) == 0);')
        checks.append(f'assert(((u8*)&{name})[{size-16}] == 255);')
    harness = """
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef struct { u8 bytes[16]; } EntityData;
enum { ROM_REGION_UNKNOWN, ROM_REGION_USA, ROM_REGION_EU, ROM_REGION_JP };
int gRomRegion;
u8 rom[0x1000000];
u8* gRomData = rom;
u32 gRomSize = sizeof(rom);
u32 Port_TranslateScriptAddr(u32 address) { return address + 0x100; }
""" + helpers + resolver + '\n'.join(declarations) + '\nvoid initialize(void) { unsigned count = 0;\n' + block + '\n(void)count; }\n'
    harness += 'int main(int argc, char** argv) { gRomRegion = atoi(argv[1]);\n'
    harness += 'if (argc == 3) { FILE* f = fopen(argv[2], "rb"); assert(f); assert(fread(rom, 1, sizeof(rom), f) == sizeof(rom)); fclose(f); } else {\n'
    harness += '\n'.join(fills) + '\n} initialize();\n' + '\n'.join(checks)
    harness += '\nEntityData compiled = {0}; assert(Port_ResolveEntityScript(&compiled, 0x08012340) == rom + 0x12440);\n'
    for name, *_ in LISTS:
        harness += f'assert(Port_ResolveEntityScript(&{name}[0], 0x08012340) == rom + 0x12340);\n'
        harness += f'assert(Port_ResolveEntityScript(&{name}[1], 0x08012340) == rom + 0x12340);\n'
    # Missing terminators must also leave an empty, bounded destination.
    harness += '\nmemset(rom, 0, sizeof(rom)); initialize();\n'
    harness += '\n'.join(f'assert(((u8*)&{row[0]})[0] == 255);' for row in LISTS)
    # Truncated/unavailable ROMs must produce empty lists instead of unbounded walks.
    harness += '\ngRomSize = 0; initialize();\n'
    harness += '\n'.join(f'assert(((u8*)&{row[0]})[0] == 255);' for row in LISTS)
    harness += '\nreturn 0; }\n'
    with tempfile.TemporaryDirectory(prefix='tmc-entity-lists-') as directory:
        path = Path(directory)
        (path / 'test.c').write_text(harness)
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
        for region, attr in ((2, 'eu'), (3, 'jp'), (1, 'usa')):
            command = [str(path / 'test'), str(region)]
            result = subprocess.run(command, capture_output=True, text=True)
            assert result.returncode == 0, result.stderr
            rom = getattr(args, attr)
            if rom:
                result = subprocess.run(command + [str(rom.resolve())], capture_output=True, text=True)
                assert result.returncode == 0, result.stderr
    print('PASS: 26 additional entity lists in USA/EU/JP, including truncated ROM fallback')


if __name__ == '__main__':
    main()
