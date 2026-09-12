#!/usr/bin/env python3
"""Run native graphics allocator regressions against production code; no ROM needed."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def main():
    command = shlex.split(os.environ.get('CC', 'cc')) + [
        '-std=gnu11', '-DPC_PORT', '-DMULTI_REGION', '-DUSA', '-DENGLISH',
        '-I.', '-Iinclude', '-Iport', '-ffunction-sections', '-fdata-sections',
        'tools/tests/gfx_slots.c', 'src/vram.c', '-Wl,--gc-sections',
    ]
    with tempfile.TemporaryDirectory(prefix='tmc-gfx-test-') as directory:
        binary = Path(directory) / 'gfx-test'
        subprocess.run(command + ['-o', str(binary)], cwd=ROOT, check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
