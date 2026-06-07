/*
 * port/port_repro_jailbars.c — Issue #149 auto-reproduction harness.
 *
 * TMC_REPRO_JAILBARS=1 drives title -> game, warps into Dark Hyrule Castle
 * B2 Prison (area 0x88 room 0x39 = ROOM_DARK_HYRULE_CASTLE_B2_PRISON), finds
 * the closed JailBars objects (object id JAIL_BARS), triggers their open flag,
 * and logs the action/animPtr/frame progression while capturing before/after
 * PNGs (jailbars_closed.png / jailbars_open.png).
 *
 * #149 root cause: pressing the prison button opens the door's COLLISION
 * (JailBars_Action1 -> SetJailBarTiles -> SetTile updates the logical map) but
 * the TEXTURE stays closed until a room reload. JailBars_Init's closed path
 * never InitializeAnimation()s, so super->animPtr is NULL when JailBars_Action2
 * calls GetNextFrame. The GBA original UpdateAnimationVariableFrames had no
 * NULL-animPtr guard and advanced off the read-protected BIOS region, which set
 * ANIM_DONE and let the door reach action 3 (which loads the open frame). The
 * PC animation system (port_animation.c) guards the NULL deref, so GetNextFrame
 * is a no-op and the door is stuck in action 2 forever.
 *
 * Exit code 0 if a jailbars reaches action >= 3 (the open path ran), 1 if it
 * stays stuck in action 2 (bug present). Combine with TMC_AUTOPLAY=1 and
 * SDL_VIDEODRIVER=dummy for a headless run.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"         /* gMain, TASK_* */
#include "entity.h"       /* Entity, GenericEntity, OBJECT, MAX_ENTITIES */
#include "object.h"       /* JAIL_BARS */
#include "flags.h"        /* SetFlag */
#include "port_gba_mem.h" /* gIoMem */

extern int Port_DebugAction_Warp(unsigned char area, unsigned char room, unsigned short x, unsigned short y,
                                 unsigned char layer);
extern int Port_CaptureBaseFramebufferPNG(const char* path);
extern GenericEntity gEntities[MAX_ENTITIES];

#define KEYINPUT_REG 0x130
#define A_BUTTON 0x0001
#define START_BUTTON 0x0008

/* Mirror of jailBars.c's private struct: flag at 0x86 GBA / 0xAE PC (its own
 * unused1[30] tail, not the GenericEntity field_0x86 union). */
typedef struct {
    Entity base;
    unsigned char unused1[30];
    unsigned short flag;
} ReproJailBars;

static Entity* repro_find_jailbars(int* count) {
    Entity* first = NULL;
    int n = 0;
    int i;
    for (i = 0; i < MAX_ENTITIES; i++) {
        Entity* e = &gEntities[i].base;
        if (e->kind == OBJECT && e->id == JAIL_BARS && e->next != NULL) {
            if (first == NULL)
                first = e;
            n++;
        }
    }
    if (count)
        *count = n;
    return first;
}

void Port_ReproJailBars_Tick(unsigned int frame) {
    static int active = -1;
    static int warp_done = 0;
    static int settle_at = 0;
    static int trigger_at = 0;
    static int triggered = 0;
    static int max_action = -1;

    if (active < 0) {
        const char* v = getenv("TMC_REPRO_JAILBARS");
        active = (v && *v && strcmp(v, "0") != 0) ? 1 : 0;
        if (active)
            fprintf(stderr, "[jailbars repro] target area=0x88 room=0x39 (B2 Prison)\n");
    }
    if (!active)
        return;

    /* Drive title + file-select (GBA KEYINPUT: 0 = pressed). */
    {
        unsigned short presses = 0;
        if (gMain.task == TASK_TITLE && frame >= 30 && (frame & 0xF) < 3)
            presses |= START_BUTTON;
        if (gMain.task == TASK_FILE_SELECT && (frame & 0xF) < 3)
            presses |= A_BUTTON;
        if (presses)
            *(volatile unsigned short*)(gIoMem + KEYINPUT_REG) &= ~presses;
    }

    if (gMain.task == TASK_GAME && !warp_done && (frame % 60 == 0) && frame > 600) {
        int rc = Port_DebugAction_Warp(0x88, 0x39, 0x80, 0x88, 1);
        fprintf(stderr, "[jailbars repro] frame %u: warp -> %d\n", frame, rc);
        if (rc == 1) {
            warp_done = 1;
            settle_at = (int)frame + 150;
        }
    }

    /* Once the room has settled, snapshot the closed door + set the open flag. */
    if (warp_done && !triggered && settle_at && (int)frame >= settle_at) {
        int cnt = 0;
        Entity* jb = repro_find_jailbars(&cnt);
        if (jb != NULL) {
            ReproJailBars* j = (ReproJailBars*)jb;
            fprintf(stderr,
                    "[jailbars repro] found %d jailbars; first: action=%u flag=0x%04X animPtr=%p frame=0x%02X\n",
                    cnt, jb->action, j->flag, (void*)jb->animPtr, jb->frame);
            Port_CaptureBaseFramebufferPNG("jailbars_closed.png");
            {
                int i;
                for (i = 0; i < MAX_ENTITIES; i++) {
                    Entity* e = &gEntities[i].base;
                    if (e->kind == OBJECT && e->id == JAIL_BARS && e->next != NULL)
                        SetFlag(((ReproJailBars*)e)->flag);
                }
            }
            triggered = 1;
            trigger_at = (int)frame;
        } else if ((int)frame > settle_at + 300) {
            fprintf(stderr, "[jailbars repro] no jailbars found in B2 Prison; bailing\n");
            fflush(stderr);
            _Exit(5);
        }
    }

    if (triggered) {
        Entity* jb = repro_find_jailbars(NULL);
        if (jb != NULL) {
            if ((int)jb->action > max_action)
                max_action = jb->action;
            fprintf(stderr,
                    "[jailbars repro] frame %u: action=%u animPtr=%p animIdx=%u frame=0x%02X frameIdx=%u\n",
                    frame, jb->action, (void*)jb->animPtr, jb->animIndex, jb->frame, jb->frameIndex);
        }
        /* Fallback: if the flag did not trigger Action1 (action never left 1),
         * force the 1 -> 2 transition so Action2 is still exercised. */
        if (jb != NULL && jb->action == 1 && (int)frame == trigger_at + 20) {
            int i;
            fprintf(stderr, "[jailbars repro] flag did not trigger; forcing action=2\n");
            for (i = 0; i < MAX_ENTITIES; i++) {
                Entity* e = &gEntities[i].base;
                if (e->kind == OBJECT && e->id == JAIL_BARS && e->next != NULL)
                    e->action = 2;
            }
        }
        if ((int)frame >= trigger_at + 120) {
            Port_CaptureBaseFramebufferPNG("jailbars_open.png");
            fprintf(stderr, "[jailbars repro] DONE max_action=%d => %s\n", max_action,
                    max_action >= 3 ? "reached open path (FIXED)" : "stuck in action 2 (#149 present)");
            fflush(stderr);
            _Exit(max_action >= 3 ? 0 : 1);
        }
    }

    if (frame == 8000) {
        fprintf(stderr, "[jailbars repro] timeout (warp_done=%d triggered=%d)\n", warp_done, triggered);
        fflush(stderr);
        _Exit(3);
    }
}
