/*
 * port/port_repro_clonebutton.c — Four Sword Sanctuary / #130 followup repros.
 *
 * Two env-gated regressions live here; both drive title -> game -> warp into
 * the sanctuary (area 0x78 room 0x01) and then inject one deterministic case.
 *
 * TMC_REPRO_CLONEBUTTON=1
 *   Live crash bugreport_20260601_010030 (SIGSEGV@0):
 *     CalculateEntityTileCollisions (movement.c:1616, hb=this->hitbox==NULL)
 *       <- sub_080044AE  <- sub_08081FF8 (button.c clone push loop)
 *   Clones spawned during the Four Sword charge are inserted into
 *   gPlayerClones[] before PlayerClone assigns super->hitbox, so a button's
 *   sub_08081FF8 pushes a NULL-hitbox clone and CalculateEntityTileCollisions
 *   derefs 0 (BIOS garbage on GBA, SIGSEGV on PC). Fix: skip NULL-hitbox
 *   clones. Harness injects a pressed button + NULL-hitbox clone and calls
 *   sub_08081FF8; without the fix it SIGSEGVs, with it prints SURVIVED.
 *
 * TMC_REPRO_GRAVEFLAG=1
 *   #130-class softlock. PushableGrave reads this->pushedFlag (GBA 0x86)
 *   directly. The WORLD_EVENT_TYPE_15 kinstone-fusion overlay (worldEvent15.c)
 *   spawns the grave with spritePtr=0 then writes the activation flag 0x80ff
 *   into GE_FIELD(field_0x86) (PC 0xB2) only — no 0xAE mirror. Pre-fix the
 *   grave read pushedFlag at PC 0xAE = 0, so pushing it ran SetFlag(0) instead
 *   of SetRoomFlag(0xFF) and WorldEvent_15_1's CheckRoomFlag(0xff) wait never
 *   cleared. Harness replicates that exact write and the grave's
 *   fully-pushed path (sub_080977F4), then asserts room flag 0xFF got set.
 *
 * Combine with TMC_AUTOPLAY=1 and SDL_VIDEODRIVER=dummy for a headless run.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "main.h"     /* gMain */
#include "entity.h"   /* Entity, gPlayerEntity */
#include "player.h"   /* gPlayerClones */
#include "object.h"   /* CreateObject, PUSHABLE_GRAVE */
#include "flags.h"    /* CheckRoomFlag, ClearFlag */
#include "port_gba_mem.h"
#include "port/port_generic_entity.h" /* GE_FIELD */

extern int Port_DebugAction_Warp(unsigned char area, unsigned char room, unsigned short x, unsigned short y,
                                 unsigned char layer);
extern void sub_08081FF8(Entity* this);  /* button.c: push player + clones off a triggered button */
extern void sub_080977F4(Entity* this);  /* pushableGrave.c: grave fully pushed -> SetFlag(pushedFlag) */

#define KEYINPUT_REG 0x130
#define A_BUTTON 0x0001
#define START_BUTTON 0x0008

enum { MODE_NONE = 0, MODE_CLONEBUTTON, MODE_GRAVEFLAG };

static void run_clonebutton_test(unsigned int frame) {
    /* A pressed button whose child is the player, with a not-yet-initialised
     * clone (hitbox == NULL) parked in gPlayerClones[0]. */
    static Entity button;
    static Entity clone;
    memset(&button, 0, sizeof button);
    memset(&clone, 0, sizeof clone);
    button.child = &gPlayerEntity.base;
    clone.collisionLayer = gPlayerEntity.base.collisionLayer;
    clone.x = gPlayerEntity.base.x;
    clone.y = gPlayerEntity.base.y;
    clone.hitbox = NULL; /* the trap */

    gPlayerClones[0] = &clone;
    fprintf(stderr, "[clonebutton] frame %u: invoking sub_08081FF8 with NULL-hitbox clone\n", frame);
    fflush(stderr);
    sub_08081FF8(&button);
    gPlayerClones[0] = NULL; /* drop the stack pointer before anything else reads it */

    fprintf(stderr, "[clonebutton] SURVIVED — clone push skipped the NULL-hitbox clone\n");
    fflush(stderr);
    _Exit(0); /* skip atexit/destructors (pre-existing port_tts teardown abort) */
}

static void run_graveflag_test(unsigned int frame) {
    Entity* g;
    int before, after;

    ClearFlag(0x80ff); /* room flag 0xFF — exactly what worldEvent15 waits on */
    before = (int)CheckRoomFlag(0xff);

    g = CreateObject(PUSHABLE_GRAVE, /*type*/ 1, /*type2*/ 2);
    if (g == NULL) {
        fprintf(stderr, "[graveflag] frame %u: CreateObject failed\n", frame);
        fflush(stderr);
        _Exit(4);
    }
    g->x = gPlayerEntity.base.x;
    g->y = gPlayerEntity.base.y;
    g->collisionLayer = gPlayerEntity.base.collisionLayer;

    /* Exactly worldEvent15.c:41 — activation flag into the aligned tail field. */
    GE_FIELD(g, field_0x86)->HWORD = 0x80ff;
    /* Grave fully pushed: runs `if (pushedFlag != 0) SetFlag(pushedFlag)`. */
    sub_080977F4(g);

    after = (int)CheckRoomFlag(0xff);
    fprintf(stderr, "[graveflag] frame %u: room flag 0xFF before=%d after=%d (expect 0 then 1)\n", frame, before,
            after);
    fflush(stderr);
    if (!before && after) {
        fprintf(stderr, "[graveflag] PASS — pushedFlag aliases field_0x86; SetRoomFlag(0xFF) fired\n");
        fflush(stderr);
        _Exit(0);
    }
    fprintf(stderr, "[graveflag] FAIL — room flag 0xFF not set; pushedFlag misaligned from field_0x86\n");
    fflush(stderr);
    _Exit(5);
}

void Port_ReproCloneButton_Tick(unsigned int frame) {
    static int mode = -1;
    static int warp_done = 0;
    static int fire_frame = 0;

    if (mode < 0) {
        const char* g = getenv("TMC_REPRO_GRAVEFLAG");
        const char* c = getenv("TMC_REPRO_CLONEBUTTON");
        if (g && *g && strcmp(g, "0") != 0)
            mode = MODE_GRAVEFLAG;
        else if (c && *c && strcmp(c, "0") != 0)
            mode = MODE_CLONEBUTTON;
        else
            mode = MODE_NONE;
        if (mode != MODE_NONE)
            fprintf(stderr, "[fs-sanctuary repro] mode=%s; target area=0x78 room=0x01\n",
                    mode == MODE_GRAVEFLAG ? "graveflag" : "clonebutton");
    }
    if (mode == MODE_NONE)
        return;

    /* Drive title + file-select (GBA KEYINPUT: 0 = pressed). */
    {
        unsigned short presses = 0;
        if (gMain.task == 0 && frame >= 30 && (frame & 0xF) < 3)
            presses |= START_BUTTON;
        if (gMain.task == 1 && (frame & 0xF) < 3)
            presses |= A_BUTTON;
        if (presses)
            *(volatile unsigned short*)(gIoMem + KEYINPUT_REG) &= ~presses;
    }

    if (gMain.task == 2 && !warp_done && (frame % 60 == 0) && frame > 600) {
        int rc = Port_DebugAction_Warp(0x78, 0x01, 388, 706, 0);
        fprintf(stderr, "[fs-sanctuary repro] frame %u: warp -> %d\n", frame, rc);
        if (rc == 1) {
            warp_done = 1;
            fire_frame = (int)(frame + 120); /* let the room finish loading */
        }
    }

    if (warp_done && fire_frame && (int)frame >= fire_frame) {
        if (mode == MODE_GRAVEFLAG)
            run_graveflag_test(frame);
        else
            run_clonebutton_test(frame);
    }

    if (frame == 6000) {
        fprintf(stderr, "[fs-sanctuary repro] timeout (warp_done=%d)\n", warp_done);
        fflush(stderr);
        _Exit(3);
    }
}
