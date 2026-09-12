#!/usr/bin/env python3
"""Regression checks for regional gameplay and issues #191/#193 (no ROM required).

Run after `xmake build -y tmc_pc`:
    python3 tools/region_regression_test.py
Pass --rom-object for a non-default build directory/platform.

The collision test links the production ROM object's regional offset tables.
The audio test compiles the production parser in isolation, without SDL/agbplay;
only its song-label lookup and active-region input are supplied by the fixture.
The barrel test includes the real manager and supplies its global engine state.
The ROM-table test extracts production accessors and regional offset definitions
to avoid pulling the complete SDL bootstrap into a standalone test.
"""
import argparse
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def run(args):
    subprocess.run(args, cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom-object', type=Path)
    args = parser.parse_args()
    objects = list((ROOT / 'build/.objs/tmc_pc').glob('**/port/port_rom.c.o'))
    obj = args.rom_object
    if obj is None:
        if len(objects) != 1:
            parser.error('build tmc_pc first, or specify --rom-object')
        obj = objects[0]
    cc = shlex.split(os.environ.get('CC', 'cc'))
    cxx = shlex.split(os.environ.get('CXX', 'c++'))
    cflags = ['-std=gnu11', '-DPC_PORT', '-DMULTI_REGION', '-DUSA',
              '-I.', '-Iinclude', '-Iport', '-ffunction-sections', '-fdata-sections']
    with tempfile.TemporaryDirectory(prefix='tmc-regression-') as directory:
        tmp = Path(directory)
        source = (ROOT / 'port/port_m4a_backend.cpp').read_text()
        start = source.index('static bool ParseIntAfterKey(')
        end = source.index('static size_t SongIdToRomPosLocked(', start)
        (tmp / 'song_map_parser.inc').write_text(source[start:end])
        rom_source = (ROOT / 'port/port_rom.c').read_text()
        start = rom_source.index('static void* ResolveActiveDataPointer(')
        end = rom_source.index('RomRegion Port_DetectRomRegion(', start)
        tables_start = rom_source.index('const RomOffsets kRomOffsets_USA =')
        tables_end = rom_source.index('/* Accessor for the active region', tables_start)
        (tmp / 'rom_table_resolvers.inc').write_text(rom_source[tables_start:tables_end] + rom_source[start:end])
        checks = [
            ('collision', cc + cflags + ['tools/tests/regional_collision.c', str(obj), '-Wl,--gc-sections']),
            ('rom_tables', cc + cflags + ['-I' + str(tmp), 'tools/tests/regional_rom_tables.c', '-Wl,--gc-sections']),
            ('barrel', cc + cflags + ['tools/tests/barrel_fall.c', '-Wl,--gc-sections']),
            ('audio', cxx + ['-std=c++17', '-I' + str(tmp), 'tools/tests/regional_song_map.cpp']),
        ]
        for name, command in checks:
            binary = tmp / name
            run(command + ['-o', str(binary)])
            run([str(binary)])


if __name__ == '__main__':
    main()
