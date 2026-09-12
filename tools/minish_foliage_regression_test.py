#!/usr/bin/env python3
"""Check production Minish path parallax selection in all regions; no ROM needed."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def main():
    flags = ['-std=gnu11', '-DPC_PORT', '-DMULTI_REGION', '-I.', '-Iinclude',
             '-Iport', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections']
    with tempfile.TemporaryDirectory(prefix='tmc-foliage-test-') as directory:
        for region in ('USA', 'EU', 'JP'):
            output = str(Path(directory) / region)
            subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + flags +
                           [f'-D{region}', 'tools/tests/minish_path_foliage.c', '-o', output],
                           cwd=ROOT, check=True)
            subprocess.run([output], cwd=ROOT, check=True)
            print(f'{region}: PASS', flush=True)


if __name__ == '__main__':
    main()
