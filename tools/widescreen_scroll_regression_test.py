#!/usr/bin/env python3
"""Exercise production rolling transition entry/completion without ROM or SDL."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
from widescreen_engine_regression_test import function

ROOT = Path(__file__).resolve().parent.parent


def main():
    scroll = (ROOT / 'src/scroll.c').read_text()
    player = (ROOT / 'src/playerUtils.c').read_text()
    names = ('void sub_080809D4(void)', 'void Scroll2Sub0(RoomControls* controls)',
             'void Scroll2Sub1(RoomControls* controls)', 'void Scroll2Sub2(RoomControls* controls)',
             'void sub_0807FEC8(RoomControls* controls)', 'void Scroll4Sub1(RoomControls* controls)')
    functions = [function(scroll, name) for name in names]
    functions.append(function(player, 'bool32 sub_0807BD14(Entity* this, u32 scrollDirection)'))
    with tempfile.TemporaryDirectory(prefix='tmc-ws-scroll-') as directory:
        (Path(directory) / 'widescreen_scroll_functions.inc').write_text('\n'.join(functions))
        binary = Path(directory) / 'test'
        command = shlex.split(os.environ.get('CC', 'cc')) + [
            '-std=gnu11', '-DPC_PORT', '-DMULTI_REGION', '-DUSA', '-DMODE1_GBA_WIDTH=384',
            '-I.', '-Iinclude', '-Iport', '-I' + directory, 'tools/tests/widescreen_scroll.c', '-o', str(binary)]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
