#!/usr/bin/env python3
"""Exercise production widescreen engine helpers without a ROM or SDL build."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    stubs = (ROOT / 'port/port_linked_stubs.c').read_text()
    message = (ROOT / 'src/message.c').read_text()
    functions = [function(stubs, name) for name in (
        'int Port_Widescreen_FallbackNative(void)',
        'int Port_Widescreen_CameraRestX(int target_x)',
        'static void Port_WidescreenShadow_Populate(',
        'static void Port_WidescreenShadow_PopulateOverlay(',
    )]
    functions.append(function(message, 'bool Message_GetWindowRect('))
    with tempfile.TemporaryDirectory(prefix='tmc-ws-engine-') as directory:
        generated = Path(directory) / 'widescreen_helpers.h'
        generated.write_text('\n'.join(functions))
        binary = Path(directory) / 'test'
        command = shlex.split(os.environ.get('CC', 'cc')) + [
            '-std=c11', '-Wall', '-Wextra', '-Werror', '-I' + directory,
            str(ROOT / 'tools/tests/widescreen_engine.c'), '-o', str(binary)]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
