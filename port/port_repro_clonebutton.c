/*
 * port/port_repro_clonebutton.c — Four Sword Sanctuary regression repros.
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
 * Drives title -> game -> warp into the sanctuary (area 0x78 room 0x01) then
 * injects the case. Combine with TMC_AUTOPLAY=1 and SDL_VIDEODRIVER=dummy for
 * a headless run.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "main.h"   /* gMain */
#include "entity.h" /* Entity, gPlayerEntity */
#include "player.h" /* gPlayerClones */
#include "port_gba_mem.h"

extern int Port_DebugAction_Warp(unsigned char area, unsigned char room, unsigned short x, unsigned short y,
                                 unsigned char layer);
extern void sub_08081FF8(Entity* this); /* button.c: push player + clones off a triggered button */

#define KEYINPUT_REG 0x130
#define A_BUTTON 0x0001
#define START_BUTTON 0x0008

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

void Port_ReproCloneButton_Tick(unsigned int frame) {
    static int active = -1;
    static int warp_done = 0;
    static int fire_frame = 0;

    if (active < 0) {
        const char* c = getenv("TMC_REPRO_CLONEBUTTON");
        active = (c && *c && strcmp(c, "0") != 0) ? 1 : 0;
        if (active)
            fprintf(stderr, "[clonebutton] harness active; target area=0x78 room=0x01\n");
    }
    if (!active)
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
        fprintf(stderr, "[clonebutton] frame %u: warp -> %d\n", frame, rc);
        if (rc == 1) {
            warp_done = 1;
            fire_frame = (int)(frame + 120); /* let the room finish loading */
        }
    }

    if (warp_done && fire_frame && (int)frame >= fire_frame)
        run_clonebutton_test(frame);

    if (frame == 6000) {
        fprintf(stderr, "[clonebutton] timeout (warp_done=%d)\n", warp_done);
        fflush(stderr);
        _Exit(3);
    }
}
