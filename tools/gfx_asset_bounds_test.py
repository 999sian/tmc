#!/usr/bin/env python3
"""Exercise production graphics replacement bounds without a ROM or player saves."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent

with tempfile.TemporaryDirectory(prefix='tmc-gfx-bounds-') as directory:
    binary = Path(directory) / 'gfx-bounds'
    command = shlex.split(os.environ.get('CXX', 'c++')) + [
        '-std=c++20', '-DPC_PORT', '-DMULTI_REGION', '-DUSA', '-DENGLISH',
        '-I.', '-Iinclude', '-Iport', '-ffunction-sections', '-fdata-sections',
        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
        'tools/tests/gfx_asset_bounds.cpp', 'port/port_asset_pak_loader.cpp',
        '-Wl,--gc-sections', '-o', str(binary),
    ]
    subprocess.run(command, cwd=ROOT, check=True)
    subprocess.run([str(binary)], check=True)
