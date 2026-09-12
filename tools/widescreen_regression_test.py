#!/usr/bin/env python3
"""Compile and run widescreen regressions without changing xmake configuration."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def main():
    with tempfile.TemporaryDirectory(prefix='tmc-widescreen-test-') as directory:
        binary = Path(directory) / 'ppu-test'
        command = shlex.split(os.environ.get('CC', 'cc')) + [
            '-std=gnu11', '-O2', '-DMODE1_GBA_WIDTH=384', '-Iport/ppu/include',
            'tools/tests/widescreen_ppu.c', 'port/ppu/src/mode1.c', '-lm', '-o', str(binary),
        ]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(binary)], check=True)
        for width in (240, 384):
            binary = Path(directory) / f'dampe-{width}'
            command = shlex.split(os.environ.get('CC', 'cc')) + [
                '-std=gnu11', '-O2', '-DPC_PORT', '-DUSA', f'-DMODE1_GBA_WIDTH={width}',
                '-I.', '-Iinclude', '-Iport', '-ffunction-sections', '-fdata-sections',
                'tools/tests/widescreen_dampe.c', '-Wl,--gc-sections', '-o', str(binary),
            ]
            subprocess.run(command, cwd=ROOT, check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
