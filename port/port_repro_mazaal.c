/*
 * port/port_repro_mazaal.c — Issue #99 auto-reproduction harness.
 *
 * When env var TMC_REPRO_MAZAAL is set, drives the game from the
 * title screen into the Fortress of Winds boss room with
 * gRoomTransition.field_0x39 forced to a mid-phase-3 value (0x14)
 * so the [mazaal] diagnostic logs in src/enemy/mazaalMacro.c
 * capture the pillar-spawn behaviour on the broken path.
 *
 * The harness fires Port_DebugAction_Warp directly — same underlying
 * call F8 → Warp → "Fortress of Winds - boss" would use — but a few
 * hundred frames after the engine has finished initializing, so we
 * don't race the title screen.
 *
 * Exit happens automatically at frame 4000 (~67 seconds at 60Hz) to
 * keep CI/headless runs bounded. The log file caller-side is what
 * matters; the renderer can be running blind.
 */

#include <stdio.h>
#include <stdlib.h>
#include "common.h"
#include "room.h"

extern int Port_DebugAction_Warp(unsigned char area, unsigned char room,
                                  unsigned short x, unsigned short y,
                                  unsigned char layer);

#include "main.h"         /* gMain, TASK_GAME */
#include "port_gba_mem.h" /* gIoMem */

#define KEYINPUT_REG 0x130

#define A_BUTTON      0x0001
#define START_BUTTON  0x0008

void Port_ReproMazaal_Tick(unsigned int frame) {
    static int active = -1;
    static int warp_done = 0;
    static int phase_set = 0;
    static int phase_set_frame = 0;
    static u8 last_task = 255;
    static unsigned int task_change_frame = 0;

    if (active < 0) {
        const char* env = getenv("TMC_REPRO_MAZAAL");
        active = (env && *env && env[0] != '0') ? 1 : 0;
        if (active) {
            fprintf(stderr, "[repro99] harness active\n");
        }
    }
    if (!active) return;

    if (gMain.task != last_task) {
        fprintf(stderr, "[repro99] frame %u: gMain.task %u → %u\n",
                frame, (unsigned)last_task, (unsigned)gMain.task);
        last_task = gMain.task;
        task_change_frame = frame;
    }

    /* Drive title + file-select automatically. We mask off bits in
     * KEYINPUT (GBA semantics: 0 = pressed). The existing port_bios
     * pulse handles the title; we extend it through file-select. */
    {
        unsigned short presses = 0;
        if (gMain.task == 0 && frame >= 60 && frame < 90) {
            presses |= START_BUTTON;
        }
        if (gMain.task == 1) {
            unsigned int dt = frame - task_change_frame;
            if ((dt >= 60 && dt < 75) ||
                (dt >= 150 && dt < 165) ||
                (dt >= 240 && dt < 255)) {
                presses |= A_BUTTON;
            }
        }
        if (presses) {
            *(volatile unsigned short*)(gIoMem + KEYINPUT_REG) &= ~presses;
        }
    }

    /* Once in TASK_GAME (2), retry the warp until it succeeds. */
    if (gMain.task == 2 && !warp_done && (frame % 60 == 0) && frame > 600) {
        int rc = Port_DebugAction_Warp(0x5A /* AREA_INNER_MAZAAL */, 0,
                                       180, 163, 1);
        fprintf(stderr, "[repro99] frame %u: warp attempt → %d\n", frame, rc);
        if (rc == 1) {
            warp_done = 1;
            phase_set_frame = (int)(frame + 240); /* let warp settle */
        }
    }

    /* After warp lands, force the phase-3 HP value so the boss enters
     * the broken state on its next phase-determination tick. */
    if (warp_done && !phase_set && (int)frame >= phase_set_frame) {
        phase_set = 1;
        unsigned char prev = gRoomTransition.field_0x39;
        gRoomTransition.field_0x39 = 0x14;
        fprintf(stderr, "[repro99] frame %u: forced field_0x39 0x%02x → 0x14 (phase 3)\n",
                frame, prev);
    }

    if (frame == 6000) {
        fprintf(stderr, "[repro99] frame %u: exit (warp_done=%d phase_set=%d)\n",
                frame, warp_done, phase_set);
        fflush(stderr);
        exit(0);
    }
}
