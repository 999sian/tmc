/*
 * port/port_repro_takeover.c — Vaati Castle takeover cutscene (#93/#109).
 *
 * The takeover cutscene's inner CUTSCENE_ORCHESTRATOR runs
 * script_CutsceneOrchestratorTakeoverCutscene (ROM 0x08015CD4), whose
 * BeginBlock..EndBlock body positions the orchestrator and CameraTargetEntity's
 * onto it to drive the King<->Vaati camera pans. #109 reported the pans don't
 * fire (the orchestrator "dies after 3 commands"); a C watchdog in
 * sub_08053BBC fakes the choreography instead.
 *
 * This harness drives title -> game, then calls sub_08053BE8() to launch the
 * takeover aux-cutscene directly (bypassing the overworld story trigger in
 * Western Woods North). The engine's existing [orch-pc] log
 * (Port_LogOrchScriptPc) prints the inner orchestrator's script instruction
 * pointer each time it advances, so we can see whether the native script runs
 * its full ~60-command choreography or stalls early.
 *
 * Enable with TMC_REPRO_TAKEOVER=1 (headless: TMC_AUTOPLAY=1,
 * SDL_VIDEODRIVER=dummy). The native orchestrator drives the cutscene by
 * default now; set TMC_TAKEOVER_WD=1 to force the old sub_08053BBC fallback
 * watchdog instead.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "main.h" /* gMain */
#include "port_gba_mem.h"

extern int Port_DebugAction_Warp(unsigned char area, unsigned char room, unsigned short x, unsigned short y,
                                 unsigned char layer);
extern void sub_08053BE8(void); /* cutscene.c: launch the takeover aux-cutscene */

#define KEYINPUT_REG 0x130
#define A_BUTTON 0x0001
#define START_BUTTON 0x0008

void Port_ReproTakeover_Tick(unsigned int frame) {
    static int active = -1;
    static int warp_done = 0;
    static int launch_frame = 0;
    static int launched = 0;

    if (active < 0) {
        const char* env = getenv("TMC_REPRO_TAKEOVER");
        active = (env && *env && strcmp(env, "0") != 0) ? 1 : 0;
        if (active)
            fprintf(stderr, "[takeover] harness active (wd=%d)\n", getenv("TMC_TAKEOVER_WD") != NULL);
    }
    if (!active)
        return;

    {
        unsigned short presses = 0;
        if (gMain.task == 0 && frame >= 30 && (frame & 0xF) < 3)
            presses |= START_BUTTON;
        if (gMain.task == 1 && (frame & 0xF) < 3)
            presses |= A_BUTTON;
        /* During the cutscene, advance dialogue textboxes (King/Vaati lines
         * use MessageNoOverlap + WaitUntilTextboxCloses, which need an A
         * press to close — the player does this on real hardware). Burst A
         * every 32 frames so each textbox is read then dismissed. */
        if (launched && (frame & 0x1F) < 2)
            presses |= A_BUTTON;
        if (presses)
            *(volatile unsigned short*)(gIoMem + KEYINPUT_REG) &= ~presses;
    }

    /* Warp to a stable Hyrule Field room (Western Woods North = area 3 room 8),
     * the overworld home of the real takeover trigger, then settle. */
    if (gMain.task == 2 && !warp_done && (frame % 60 == 0) && frame > 600) {
        int rc = Port_DebugAction_Warp(0x03, 0x08, 0x1d8, 0x138, 0);
        fprintf(stderr, "[takeover] frame %u: warp -> %d\n", frame, rc);
        if (rc == 1) {
            warp_done = 1;
            launch_frame = (int)(frame + 150);
        }
    }

    if (warp_done && !launched && launch_frame && (int)frame >= launch_frame) {
        launched = 1;
        fprintf(stderr, "[takeover] frame %u: launching takeover cutscene (sub_08053BE8)\n", frame);
        fflush(stderr);
        sub_08053BE8();
    }

    /* Let the cutscene play out, then exit. The [orch-pc]/[wd] logs print
     * automatically along the way. */
    if (launched && (int)frame >= launch_frame + 7000) {
        fprintf(stderr, "[takeover] done observing; exiting\n");
        fflush(stderr);
        _Exit(0);
    }

    if (frame == 14000) {
        fprintf(stderr, "[takeover] timeout (warp_done=%d launched=%d)\n", warp_done, launched);
        fflush(stderr);
        _Exit(3);
    }
}
