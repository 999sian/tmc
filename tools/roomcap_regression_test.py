#!/usr/bin/env python3
"""Run the production room-tour block with deterministic warp/capture boundaries."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    source = (ROOT / 'port/port_repro_roomcap.c').read_text()
    tour_start = source.index('    /* TMC_ROOMCAP_TOUR=<plan>')
    tour_end = source.index('    /* Once stable in TASK_GAME', tour_start)
    timeout = function(source, '    if (frame >= 5000')
    helpers = function(source, 'static int RoomCap_ParseTourRow')
    harness = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <stdint.h>
typedef unsigned int u32;
typedef unsigned char u8;
typedef struct Entity {
 struct Entity* next; int id, flags;
 struct { int draw; } spriteSettings;
 struct { struct { int HI; } HALF; } x,y;
} Entity;
typedef struct { void* scriptInstructionPointer; } ScriptExecutionContext;
static struct { int task,substate; } gMain;
static struct { int hideFlags; } gHUD;
static struct { Entity base; } gPlayerEntity;
static struct { int controlMode; } gPlayerState;
static struct { int state; } gMessage;
static struct { struct { int health,maxHealth; } stats; int global_progress; } gSave;
static struct { int area,room,scroll_x,scroll_y,origin_x,origin_y,width,height; Entity* camera_target; } gRoomControls;
static struct { Entity* first; } gEntityLists[9];
static u8* gRomData;
enum { TASK_TITLE, TASK_GAME, GAMEMAIN_UPDATE=2, GAMEMAIN_BARRELUPDATE,
 CONTROL_ENABLED, MESSAGE_ACTIVE=1, HUD_HIDE_ALL=255, ENT_SCRIPTED=1,
 LV1_CLEAR,LV2_CLEAR,LV3_CLEAR,LV4_CLEAR,LV5_CLEAR,LV6_CLEAR,TABIDACHI,
 ITEM_EARTH_ELEMENT,ITEM_FIRE_ELEMENT,ITEM_WATER_ELEMENT,ITEM_WIND_ELEMENT,ITEM_KINSTONE_BAG };
static void SetGlobalFlag(u32 flag) {}
static void SetInventoryValue(int item,int value) {}
static void UpdateGlobalProgress(void) {}
static ScriptExecutionContext* Port_GetEntityScriptCtx(Entity* e) { return NULL; }
void Port_Config_TestForceEdge(int input) {}
static int Port_DebugAction_Warp(unsigned char area,unsigned char room,unsigned short x,unsigned short y,unsigned char l) {
 if (getenv("TEST_NO_ARRIVAL")) return 0;
 gRoomControls.area=area; gRoomControls.room=room; return 1;
}
static int captures, polls;
static int Capture(const char* path) {
 if (getenv("TEST_CAPTURE_FAIL")) return -1;
 if (getenv("TEST_CAPTURE_PENDING")) return 0;
 if (getenv("TMC_ROOMCAP_PRESENTATION") && polls++ % 3 != 2) return 0;
 FILE* f=fopen(path,"w"); if(!f) return -1;
 fprintf(f,"%d",++captures); assert(fclose(f)==0); return 1;
}
int Port_CaptureBaseFramebufferPNG(const char* path) { return Capture(path)>0; }
int Port_CapturePresentationPNG(const char* path) { return Capture(path); }
''' + function(source, 'static int RoomCap_Capture') + '\n' + helpers + r'''
static void Tick(unsigned int frame) {
 static int tour_running;
 int booted=!getenv("TEST_BOOTSTRAP"), settle=getenv("TEST_SETTLE")?atoi(getenv("TEST_SETTLE")):1;
''' + timeout + '\n' + source[tour_start:tour_end] + r'''
}
int main(void) {
 gMain.task=getenv("TEST_BOOTSTRAP")?TASK_TITLE:TASK_GAME;
 gMain.substate=GAMEMAIN_UPDATE;
 gPlayerState.controlMode=CONTROL_ENABLED;
 gRoomControls.camera_target=&gPlayerEntity.base;
 for(unsigned frame=1;frame<15000;++frame) {
  if(getenv("TEST_TASK_STUCK") && frame>100) gMain.task=TASK_TITLE;
  Tick(frame);
 }
 return 99;
}
'''
    with tempfile.TemporaryDirectory(prefix='tmc-roomcap-test-') as directory:
        tmp = Path(directory)
        (tmp / 'tour.c').write_text(harness)
        command = shlex.split(os.environ.get('CC', 'cc'))
        subprocess.run(command + ['-std=c11', '-Wno-pointer-to-int-cast', str(tmp / 'tour.c'),
                                 '-o', str(tmp / 'tour')], check=True)
        tests = [
            ('negative-start', '3 8 1 2\n', '-1', {}, 2, 0),
            ('overflow-start', '3 8 1 2\n', '9999999999999999999999999999', {}, 2, 0),
            ('whitespace-start', '3 8 1 2\n', '  ', {}, 2, 0),
            ('trailing-start', '3 8 1 2\n', '0junk', {}, 2, 0),
            ('past-end-start', '3 8 1 2\n', '2', {}, 2, 0),
            ('resume-at-end', '3 8 1 2\n', '1', {}, 0, 0),
            ('missing-plan', None, '0', {}, 2, 0),
            ('unreadable-plan', None, '0', {}, 2, 0),
            ('empty-plan', '', '0', {}, 2, 0),
            ('too-long-line', '#' * 256 + '\n3 8 1 2\n', '0', {}, 2, 0),
            ('malformed-plan', '3 8 nope 2\n', '0', {}, 2, 0),
            ('trailing-row', '3 8 1 2 1 junk\n', '0', {}, 2, 0),
            ('negative-row', '3 8 -1 2\n', '0', {}, 2, 0),
            ('wide-area', '256 8 1 2\n', '0', {}, 2, 0),
            ('wide-room', '3 256 1 2\n', '0', {}, 2, 0),
            ('wide-coordinate', '3 8 65536 2\n', '0', {}, 2, 0),
            ('wide-layer', '3 8 1 2 256\n', '0', {}, 2, 0),
            ('overflow-row', '3 8 9999999999999999999999 2\n', '0', {}, 2, 0),
            ('png-failure', '3 8 1 2\n', '0', {'TEST_CAPTURE_FAIL': '1'}, 2, 0),
            ('capture-stuck', '3 8 1 2\n', '0', {'TEST_CAPTURE_PENDING': '1', 'TMC_ROOMCAP_PRESENTATION': '1'}, 3, 0),
            ('output-path-too-long', '3 8 1 2\n', '0', {'TMC_ROOMCAP_TOUR_OUT': 'x' * 510}, 2, 0),
            ('task-stuck', '3 8 1 2\n', '0', {'TEST_TASK_STUCK': '1', 'TEST_SETTLE': '700'}, 3, 0),
            ('never-arrived', '3 8 1 2\n', '0', {'TEST_NO_ARRIVAL': '1'}, 3, 0),
            ('bootstrap-timeout', '3 8 1 2\n', '0', {'TEST_BOOTSTRAP': '1'}, 3, 0),
            ('presentation-pending', '# views\n0x3 0x8 1 2 1\n3 8 4 5\n', '0',
             {'TMC_ROOMCAP_PRESENTATION': '1'}, 0, 2),
            ('long-tour', '3 8 1 2\n' * 8, '0', {'TEST_SETTLE': '700'}, 0, 8),
            ('no-final-newline', '3 8 1 2', '0', {}, 0, 1),
            ('field-limits', '255 255 65535 65535 255\n', '0', {}, 0, 1),
            ('resume', '3 8 1 2\n' * 3, '1', {}, 0, 2),
            ('too-many-views', '3 8 1 2\n' * 4097, '0', {}, 2, 0),
        ]
        failed = []
        for name, plan, start, options, code, count in tests:
            case = tmp / name
            case.mkdir()
            if name == 'unreadable-plan':
                (case / 'plan').mkdir()
            if plan is not None:
                (case / 'plan').write_text(plan)
            env = {k:v for k,v in os.environ.items() if not k.startswith(('TMC_ROOMCAP', 'TEST_'))}
            env.update(TMC_ROOMCAP_TOUR=str(case / 'plan'), TMC_ROOMCAP_TOUR_START=start,
                       TMC_ROOMCAP_TOUR_OUT=str(case))
            env.update(options)
            result = subprocess.run([str(tmp / 'tour')], env=env, capture_output=True, text=True)
            images = sorted(case.glob('*.png'))
            passed = result.returncode == code and len(images) == count
            if name == 'presentation-pending':
                passed &= [p.read_text() for p in images] == ['1', '2']
            print(f'{name}: {"PASS" if passed else "FAIL"}')
            if not passed:
                failed.append(name)
                print(f'expected exit={code}, captures={count}; got exit={result.returncode}, captures={len(images)}')
                print(result.stderr[-1200:])
        assert not failed, failed


if __name__ == '__main__':
    main()
