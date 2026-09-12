#!/usr/bin/env python3
"""Check the production room-region visibility gate used by exterior doors."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
source = (ROOT / 'src/main.c').read_text()
start = source.index('u32 CheckRegionOnScreen(')
end = source.index('\n}', start) + 2
harness = r'''
#include <assert.h>
#include <stdint.h>
typedef uint32_t u32;
#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 160
#define TRUE 1
#define FALSE 0
struct { int scroll_x, scroll_y, origin_x, origin_y; } gRoomControls;
static int width;
int Port_Widescreen_EffectiveViewWidth(void) { return width; }
'''
harness += source[start:end]
harness += r'''
int main(void) {
    /* Festival door at (392,291), camera at (8,248): visible only when wide. */
    gRoomControls.scroll_x = 8;
    gRoomControls.scroll_y = 248;
    width = 384;
    assert(CheckRegionOnScreen(392,291,32,32));
    width = 240;
    assert(!CheckRegionOnScreen(392,291,32,32));
    const int widths[] = {240, 284, 320, 384};
    for (unsigned i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i) {
        width = widths[i];
        /* Original inclusive near-edge bounds, with a nonzero room origin. */
        gRoomControls.origin_x = 1000;
        gRoomControls.scroll_x = 1008;
        assert(CheckRegionOnScreen(8 + width,291,32,32));
        assert(!CheckRegionOnScreen(9 + width,291,32,32));
        assert(CheckRegionOnScreen(0,291,32,32));
        assert(!CheckRegionOnScreen(8,409,32,32));
    }
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='tmc-door-visibility-') as directory:
    source_path = Path(directory) / 'test.c'
    source_path.write_text(harness)
    binary = Path(directory) / 'test'
    subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + [
        '-std=c11', '-DPC_PORT', '-Wall', '-Wextra', '-Werror',
        str(source_path), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('PASS: festival door visibility and native/wide viewport boundaries')
