#!/usr/bin/env python3
"""Exercise production deferred-shadow coordinates and actor fade placement."""
from pathlib import Path
import os
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
    draw = (ROOT / 'port/port_draw.c').read_text()
    start = draw.rfind('typedef struct {', 0, draw.index('packed0;'))
    end = draw.index('/* ---- CheckOnScreen', start)
    enqueue_start = draw.index('    if (sDeferredList.count >= 64)')
    enqueue_end = draw.index('\n}', enqueue_start)
    shadow = '''#include <stdint.h>
#include <assert.h>
#include <stddef.h>
typedef int16_t s16; typedef int32_t s32;
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;
typedef struct { u8 spriteSettings; } Entity;
static struct { int updated; } gOAMControls;
static const u8 frame[] = {1};
static const u8* sShadowFramePtrs[4] = {frame,frame,frame,frame};
static int sShadowTableLoaded = 1;
static int actualX, actualY, actualPriority;
static void LoadShadowTableFromRom(void) {}
static void RenderSpritePieces(const u8* data,s16 x,s16 y,u32 flags,u16 extra) {
 assert(data==frame); actualX=x; actualY=y; actualPriority=extra>>10;
}
''' + draw[start:end] + '''
static void enqueue(int x,int deferY,int extra,int type) {
 Entity storage = { (u8)(type<<4) }; Entity* entity=&storage;
''' + draw[enqueue_start:enqueue_end] + '\n}\n' + function(draw, 'static void ProcessDeferredList(void) {') + '''
int main(void) {
 const int xs[]={520,-520,0,383};
 for (unsigned i=0;i<sizeof(xs)/sizeof(xs[0]);i++) {
  sDeferredList.count=0; enqueue(xs[i],-160,3<<10,2); ProcessDeferredList();
  assert(actualX==xs[i]); assert(actualY==-160); assert(actualPriority==3);
 }
}
'''
    voxel = (ROOT / 'port/port_voxel.cpp').read_text()
    fade = '''#include <algorithm>
#include <cassert>
#include <cstddef>
constexpr int kFadeMaxActors=16; constexpr float kTopLayerLift=16;
struct Fixed { struct { int HI; } HALF; };
struct Entity { Entity* next; int kind,collisionLayer; Fixed x,y,z; };
struct LinkedList { Entity* first; };
struct { Entity base; } gPlayerEntity;
LinkedList gEntityLists[9];
struct { int origin_x,origin_y; } gRoomControls;
constexpr int ENEMY=3;
struct PortVoxelFade { float actors[kFadeMaxActors][4]; int count[4]; };
''' + function(voxel, 'static void GatherFadeActors(PortVoxelFade& f) {') + '''
int main() {
 PortVoxelFade f={}; gPlayerEntity.base.collisionLayer=2;
 gPlayerEntity.base.x.HALF.HI=40; gPlayerEntity.base.y.HALF.HI=50;
 GatherFadeActors(f); assert(f.count[0]==1); assert(f.actors[0][1]==16);
 assert(f.actors[0][0]==40 && f.actors[0][2]==50);
 gPlayerEntity.base.z.HALF.HI=-8; GatherFadeActors(f); assert(f.actors[0][1]==24);
 gPlayerEntity.base.collisionLayer=1; GatherFadeActors(f); assert(f.actors[0][1]==8);
}
'''
    with tempfile.TemporaryDirectory(prefix='tmc-voxel-test-') as directory:
        tmp = Path(directory)
        for name, source, compiler, flags in [('shadow', shadow, 'CC', ['-std=c11']),
                                               ('fade', fade, 'CXX', ['-std=c++17'])]:
            path = tmp / (name + ('.c' if compiler == 'CC' else '.cpp'))
            path.write_text(source)
            command = shlex.split(os.environ.get(compiler, 'cc' if compiler == 'CC' else 'c++'))
            subprocess.run(command + flags + [str(path), '-o', str(tmp / name)], check=True)
            subprocess.run([str(tmp / name)], check=True)
            print(name + ': PASS')


if __name__ == '__main__':
    main()
