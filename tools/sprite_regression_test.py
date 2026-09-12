#!/usr/bin/env python3
"""Exercise production regional sprite assignment boundaries without a ROM."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def main():
    source = (ROOT / 'src/ui.c').read_text()
    struct_start = source.index('typedef struct {')
    struct_end = source.index('} UIElementDefinition;', struct_start) + len('} UIElementDefinition;')
    init_start = source.index('void Port_InitUIElementDefinitions(void) {')
    init_end = source.index('\n}', init_start) + 2
    with tempfile.TemporaryDirectory(prefix='tmc-sprites-') as directory:
        tmp = Path(directory)
        # Retain the real initializer and its real struct, avoiding unrelated UI engine dependencies.
        (tmp / 'ui_initializer.inc').write_text(source[struct_start:struct_end] +
            '\nUIElementDefinition gUIElementDefinitions[11];\n' + source[init_start:init_end])
        command = shlex.split(os.environ.get('CC', 'cc')) + [
            '-std=gnu11', '-DPC_PORT', '-DMULTI_REGION', '-I.', '-Iinclude', '-Iport',
            '-I' + str(tmp), '-ffunction-sections', '-fdata-sections',
            'tools/tests/regional_sprites.c', 'src/projectileUtils.c', 'src/enemyUtils.c',
            '-Wl,--gc-sections', '-o', str(tmp / 'sprites')]
        for compiled_region in ('USA', 'EU'):
            subprocess.run(command + ['-D' + compiled_region], cwd=ROOT, check=True)
            subprocess.run([str(tmp / 'sprites')], cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
