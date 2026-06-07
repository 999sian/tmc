/*
 * port/port_repro_credits.c — "credits don't load" reproduction harness.
 *
 * TMC_REPRO_CREDITS=1 drives title -> file-select -> game like the other
 * harnesses, then once in TASK_GAME triggers the credits through the EXACT
 * path the ending uses: it resolves the outro script's "Call sub_0807FB94 @
 * Roll Credits" target (GBA Thumb 0x0807FB95) via Port_LookupScriptFunc and
 * calls it. This isolates "do the credits start + render" from "does the long
 * Vaati ending cutscene reach the Call", and it regression-guards the script-
 * func registration: if the lookup is NULL (the original bug) it exits(3).
 *
 * It logs every gMain.state / gMenu.menuType transition and writes a few PNG
 * snapshots of the base framebuffer so a blank/black credits screen is visible
 * headless:
 *   TMC_REPRO_CREDITS=1 TMC_AUTOPLAY=1 SDL_VIDEODRIVER=dummy \
 *     SDL_AUDIODRIVER=dummy ./tmc_pc
 * Snapshots: credits_<delta>.png (delta = frames since the credits started).
 */

#include <stdio.h>
#include <stdlib.h>
#include "main.h"         /* gMain, TASK_GAME, TASK_STAFFROLL */
#include "menu.h"         /* gMenu */
#include "save.h"         /* gSaveHeader */
#include "port_gba_mem.h" /* gIoMem */

extern int Port_CaptureBaseFramebufferPNG(const char* path); /* port_bugreport.cpp */

#define KEYINPUT_REG 0x130
#define A_BUTTON 0x0001
#define START_BUTTON 0x0008

void Port_ReproCredits_Tick(unsigned int frame) {
    static int active = -1;
    static int forced = 0;
    static unsigned int start_frame = 0;
    static unsigned char last_task = 255;
    static unsigned int task_change_frame = 0;
    static unsigned char last_state = 255;
    static unsigned char last_menu = 255;
    static int shot_idx = 0;
    static int reset_requested = 0;
    static unsigned int reset_frame = 0;

    static const unsigned int kShots[] = { 30, 120, 250, 450, 800 };
    static const unsigned int kNumShots = sizeof(kShots) / sizeof(kShots[0]);

    if (active < 0) {
        const char* env = getenv("TMC_REPRO_CREDITS");
        active = (env && *env && env[0] != '0') ? 1 : 0;
        if (active) {
            fprintf(stderr, "[credits] harness active\n");
        }
    }
    if (!active) {
        return;
    }

    if (gMain.task != last_task) {
        fprintf(stderr, "[credits] frame %u: gMain.task %u -> %u\n", frame, (unsigned)last_task,
                (unsigned)gMain.task);
        last_task = gMain.task;
        task_change_frame = frame;
    }

    /* Drive title + file-select (GBA KEYINPUT: 0 = pressed). The title and
     * file-select advance on key *edges* (gInput.newKeys), so pulse — press
     * for 2 frames, release for the rest of a 16-frame period — to generate a
     * fresh edge regularly until we're past each screen. A single held window
     * yields only one edge and races fades/state setup. */
    if (!forced) {
        unsigned short presses = 0;
        int pulse = (frame % 16) < 2; /* on for 2 of every 16 frames */
        if (gMain.task == TASK_TITLE && frame >= 120 && pulse) {
            presses |= START_BUTTON | A_BUTTON;
        }
        if (gMain.task == TASK_FILE_SELECT && pulse) {
            presses |= A_BUTTON;
        }
        if (presses) {
            *(volatile unsigned short*)(gIoMem + KEYINPUT_REG) &= ~presses;
        }
    }

    /* Once we're solidly in-game, trigger the credits through the EXACT path
     * the outro script uses: ScriptCommand_Call resolves the GBA Thumb address
     * 0x0807FB95 (sub_0807FB94 | 1) via Port_LookupScriptFunc, then calls it.
     * Before the registration fix the lookup returned NULL and credits never
     * started; this verifies the fix end-to-end. sub_0807FB94 ignores its
     * (entity, context) args, so NULL is safe. */
    if (!forced && gMain.task == TASK_GAME && frame - task_change_frame > 180) {
        extern void* Port_LookupScriptFunc(unsigned int gba_addr);
        typedef void (*ScriptCmd)(void*, void*);
        ScriptCmd roll_credits = (ScriptCmd)Port_LookupScriptFunc(0x0807FB95u);
        fprintf(stderr,
                "[credits] frame %u: lang=%u  Port_LookupScriptFunc(0x0807FB95)=%p (NULL=bug)\n",
                frame, (unsigned)gSaveHeader->language, (void*)roll_credits);
        if (!roll_credits) {
            fprintf(stderr, "[credits] FAIL: credits Call target unregistered\n");
            fflush(stderr);
            exit(3);
        }
        roll_credits(0, 0); /* == outro line 59 "Call sub_0807FB94 @ Roll Credits" */
        forced = 1;
        start_frame = frame;
        last_state = 255;
        last_menu = 255;
        return;
    }

    if (!forced) {
        return;
    }

    /* After the soft reset the game must be back at the title and still
     * looping — proving SoftReset() restarted to title instead of exit()/abort
     * (the original credits-end crash). */
    if (reset_requested) {
        if (gMain.task == TASK_TITLE) {
            fprintf(stderr,
                    "[credits] PASS: soft reset returned to TASK_TITLE %u frames after "
                    "end-of-credits DoSoftReset()\n",
                    frame - reset_frame);
            fflush(stderr);
            _Exit(0);
        }
        if (frame - reset_frame > 600) {
            fprintf(stderr, "[credits] FAIL: no return to title %u frames after reset (task=%u)\n",
                    frame - reset_frame, (unsigned)gMain.task);
            fflush(stderr);
            _Exit(4);
        }
        return;
    }

    /* Log state/menuType progression of the staffroll task. */
    if (gMain.task == TASK_STAFFROLL && (gMain.state != last_state || gMenu.menuType != last_menu)) {
        fprintf(stderr, "[credits] +%u: state=%u menuType=%u overlay=%u timer=%u\n",
                frame - start_frame, (unsigned)gMain.state, (unsigned)gMenu.menuType,
                (unsigned)gMenu.overlayType, (unsigned)gMenu.transitionTimer);
        last_state = gMain.state;
        last_menu = gMenu.menuType;
    }

    if (shot_idx < (int)kNumShots && frame - start_frame >= kShots[shot_idx]) {
        char path[64];
        snprintf(path, sizeof(path), "credits_%u.png", kShots[shot_idx]);
        int ok = Port_CaptureBaseFramebufferPNG(path);
        fprintf(stderr, "[credits] +%u: captured %s -> %d\n", frame - start_frame, path, ok);
        shot_idx++;
    }

    /* Once the credits have demonstrably rendered, exercise the end-of-credits
     * path exactly as StaffrollTask_State3 does: DoSoftReset(). It longjmp()s
     * back into AgbMain (control does not return here). */
    if (frame - start_frame >= 900) {
        extern void DoSoftReset(void);
        fprintf(stderr, "[credits] +%u: DoSoftReset() (end-of-credits restart)\n",
                frame - start_frame);
        fflush(stderr);
        reset_requested = 1;
        reset_frame = frame;
        DoSoftReset();
        return;
    }
}
