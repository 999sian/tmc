#!/usr/bin/env python3
"""Check the production enemy home initializer against PC room-spawn layout."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def main():
    with tempfile.TemporaryDirectory(prefix='tmc-enemy-home-') as directory:
        for region in ('USA', 'EU', 'JP'):
            binary = Path(directory) / region
            subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + [
                '-std=gnu11', '-DPC_PORT', '-D' + region, '-I.', '-Iinclude', '-Iport',
                '-ffunction-sections', '-fdata-sections', 'tools/tests/enemy_home.c',
                'src/enemyUtils.c', '-Wl,--gc-sections', '-o', str(binary),
            ], cwd=ROOT, check=True)
            subprocess.run([str(binary)], check=True)
            print(region + ': PASS', flush=True)


if __name__ == '__main__':
    main()
