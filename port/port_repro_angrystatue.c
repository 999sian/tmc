/*
 * port/port_repro_angrystatue.c — Issue #77 reward-flag auto-reproduction.
 *
 * TMC_REPRO_ANGRYSTATUE=1 drives title -> game, warps into Dark Hyrule Castle
 * 1F Loop Left (area 0x88 room 0x26 = ROOM_DARK_HYRULE_CASTLE_1F_LOOP_LEFT),
 * the angry-statue puzzle room. It finds the AngryStatueManager, logs its
 * completion flag (field_0x3e), then forces all four AngryStatues into their
 * "destroyed" state (action 3) simultaneously — the synchronized deflect the
 * player performs — and checks that the manager observes field_0x36==0xf,
 * completes, and SetFlag(field_0x3e) fires (CheckFlags becomes true).
 *
 * #77 followups:
 *  - AngryStatue_Action3 signalled the manager via parent->z.BYTES.byte2 which
 *    aliased field_0x36 on GBA but corrupted field_0x20[0] on PC (fixed in
 *    angryStatue.c) -> the deflect crash.
 *  - The manager's completion flag field_0x3e (= paramC>>16) is mis-mapped by
 *    RegisterRoomEntity's MemCopy on PC (Manager + field_0x20[4] both widened),
 *    so it stayed 0 and SetFlag(0) fired on completion -> the reward (the four
 *    pillars dropping) never triggered. Fixed in room.c (LoadRoomEntity).
 *
 * Exit 0 if field_0x3e is non-zero AND the completion flag gets set; 1 if the
 * flag is still 0 / never set. Combine with TMC_AUTOPLAY=1 + dummy SDL drivers.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"                         /* gMain, TASK_* */
#include "entity.h"                       /* Entity, GenericEntity, OBJECT, MANAGER, MAX_ENTITIES */
#include "object.h"                       /* ANGRY_STATUE */
#include "manager.h"                      /* ANGRY_STATUE_MANAGER */
#include "manager/angryStatueManager.h"  /* AngryStatueManager, field_0x3e */
#include "flags.h"                        /* CheckFlags */
#include "port_gba_mem.h"                 /* gIoMem */

extern int Port_DebugAction_Warp(unsigned char area, unsigned char room, unsigned short x, unsigned short y,
                                 unsigned char layer);
extern int Port_CaptureBaseFramebufferPNG(const char* path);
extern Entity* FindEntityByID(u32 kind, u32 id, u32 listIndex);
extern GenericEntity gEntities[MAX_ENTITIES];

#define KEYINPUT_REG 0x130
#define A_BUTTON 0x0001
#define START_BUTTON 0x0008

static int force_statues_action3(void) {
    int n = 0;
    int i;
    for (i = 0; i < MAX_ENTITIES; i++) {
        Entity* e = &gEntities[i].base;
        if (e->kind == OBJECT && e->id == ANGRY_STATUE && e->next != NULL) {
            e->action = 3;     /* "destroyed" countdown — sets field_0x36 bit each frame */
            e->timer = 30;
            n++;
        }
    }
    return n;
}

void Port_ReproAngryStatue_Tick(unsigned int frame) {
    static int active = -1;
    static int warp_done = 0;
    static int settle_at = 0;
    static int forced_at = 0;
    static int forced = 0;
    static unsigned short flag = 0;

    if (active < 0) {
        const char* v = getenv("TMC_REPRO_ANGRYSTATUE");
        active = (v && *v && strcmp(v, "0") != 0) ? 1 : 0;
        if (active)
            fprintf(stderr, "[angrystatue repro] target area=0x88 room=0x26 (1F Loop Left)\n");
    }
    if (!active)
        return;

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
        int rc = Port_DebugAction_Warp(0x88, 0x26, 0x78, 0x78, 1);
        fprintf(stderr, "[angrystatue repro] frame %u: warp -> %d\n", frame, rc);
        if (rc == 1) {
            warp_done = 1;
            settle_at = (int)frame + 150;
        }
    }

    if (warp_done && !forced && settle_at && (int)frame >= settle_at) {
        Entity* mgr = FindEntityByID(MANAGER, ANGRY_STATUE_MANAGER, 6);
        int statues;
        if (mgr != NULL) {
            flag = ((AngryStatueManager*)mgr)->field_0x3e;
            fprintf(stderr, "[angrystatue repro] manager found: field_0x3e(flag)=0x%04X action=%u\n",
                    flag, mgr->action);
            Port_CaptureBaseFramebufferPNG("angrystatue_before.png");
            statues = force_statues_action3();
            fprintf(stderr, "[angrystatue repro] forced %d statues into action 3\n", statues);
            forced = 1;
            forced_at = (int)frame;
        } else if ((int)frame > settle_at + 300) {
            fprintf(stderr, "[angrystatue repro] no AngryStatueManager found; bailing\n");
            fflush(stderr);
            _Exit(5);
        }
    }

    if (forced) {
        Entity* mgr = FindEntityByID(MANAGER, ANGRY_STATUE_MANAGER, 6);
        int statue_flag_set = (flag != 0) ? (CheckFlags(flag) != 0) : 0;
        int bollards = 0, bollard_max_action = -1;
        int i;
        for (i = 0; i < MAX_ENTITIES; i++) {
            Entity* e = &gEntities[i].base;
            if (e->kind == OBJECT && e->id == BOLLARD && e->next != NULL) {
                bollards++;
                if ((int)e->action > bollard_max_action)
                    bollard_max_action = e->action;
            }
        }
        if ((int)frame % 30 == 0)
            fprintf(stderr, "[angrystatue repro] frame %u: mgr=%s statueFlag(0x%02X)=%d bollards=%d maxAction=%d\n",
                    frame, mgr ? "alive" : "gone", flag, statue_flag_set, bollards, bollard_max_action);
        if ((int)frame >= forced_at + 300) {
            /* Bollards (the 4 "pillars") leave action 1 and reach action 2/3
             * once they start dropping, after the camera manager relays the
             * statue-completion flag (0x61 -> 0x62). */
            int dropped = (bollard_max_action >= 2);
            int ok = (flag != 0) && statue_flag_set && dropped;
            Port_CaptureBaseFramebufferPNG("angrystatue_after.png");
            fprintf(stderr, "[angrystatue repro] DONE statueFlag=0x%04X set=%d bollardMaxAction=%d => %s\n",
                    flag, statue_flag_set, bollard_max_action,
                    ok ? "statues destroyed + pillars dropped (FIXED)" : "chain incomplete (#77 present)");
            fflush(stderr);
            _Exit(ok ? 0 : 1);
        }
    }

    if (frame == 8000) {
        fprintf(stderr, "[angrystatue repro] timeout (warp_done=%d forced=%d)\n", warp_done, forced);
        fflush(stderr);
        _Exit(3);
    }
}
