#!/usr/bin/env python3
"""Compile actual allocation handlers with deterministic failures; no ROM needed."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def main():
    flags = ['-std=gnu11', '-DPC_PORT', '-DMULTI_REGION', '-DUSA', '-I.', '-Iinclude',
             '-Iport', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections']
    with tempfile.TemporaryDirectory(prefix='tmc-allocation-test-') as directory:
        for name in ('chest_reward', 'death_fx_allocation', 'npc_gfx_allocation'):
            output = str(Path(directory) / name)
            subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + flags +
                           [f'tools/tests/{name}.c', '-o', output], cwd=ROOT, check=True)
            subprocess.run([output], cwd=ROOT, check=True)
            print(f'{name}: PASS', flush=True)


if __name__ == '__main__':
    main()
