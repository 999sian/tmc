#!/usr/bin/env python3
"""After a tools CMake build, compare embedded converters with standalone targets.

Usage: python3 tools/tests/asset_processor_cmake.py /tmp/tmc-tools-build
All input/output is synthetic and confined to a temporary directory.
"""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

build = Path(sys.argv[1]).resolve() / 'src'
processor = build / 'asset_processor/asset_processor'
gbagfx = build / 'gbagfx/gbagfx'
aif2pcm = build / 'aif2pcm/aif2pcm'

with tempfile.TemporaryDirectory(prefix='tmc-converter-test-') as directory:
    root = Path(directory)
    (root / 'assets').mkdir()
    (root / 'out').mkdir()
    (root / 'baserom.gba').write_bytes(b'\0')
    (root / 'assets/test.json').write_text(json.dumps([
        {'type': 'gfx', 'path': 'tile.4bpp'},
        {'type': 'aif', 'path': 'sample.bin'},
    ]))
    (root / 'out/tile.4bpp').write_bytes(bytes(range(32)))
    (root / 'out/sample.bin').write_bytes(struct.pack('<4I', 0, 22050 * 1024, 0, 31) + bytes(range(32)))

    def run(*args):
        subprocess.run([str(arg) for arg in args], cwd=root, check=True,
                       env={**os.environ, 'TMC_ASSET_JOBS': '2'})

    run(gbagfx, 'out/tile.4bpp', 'expected.png')
    run(aif2pcm, 'out/sample.bin', 'expected.aif')
    run(processor, 'convert', 'USA', 'out')
    assert (root / 'out/tile.png').read_bytes() == (root / 'expected.png').read_bytes()
    assert (root / 'out/sample.aif').read_bytes() == (root / 'expected.aif').read_bytes()
    run(gbagfx, 'out/tile.png', 'expected.4bpp')
    run(aif2pcm, 'out/sample.aif', 'expected.bin')
    run(processor, 'build', 'USA', 'out')
    assert (root / 'out/tile.4bpp').read_bytes() == (root / 'expected.4bpp').read_bytes()
    assert (root / 'out/sample.bin').read_bytes() == (root / 'expected.bin').read_bytes()
    print('CMAKE EMBEDDED/STANDALONE CONVERTERS OK')
