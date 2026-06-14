/*
 * port/port_repro_roomcap.c — generic in-game room capture harness.
 *
 * Bootstraps a fresh new game directly (no save file required): presses START
 * at the title to reach file-select, then synthesizes a clean save whose start
 * room is the target, activates it, and enters TASK_GAME. Once in-game it warps
 * to the target room (re-rendered from the ACTIVE ROM) and dumps the base
 * framebuffer. The capture therefore reflects the loaded region's true
 * rendering of a known room — ideal for per-region comparison.
 *
 * Enable with TMC_ROOMCAP=1 (headless: TMC_AUTOPLAY=1, SDL_VIDEODRIVER=dummy).
 *   TMC_ROOMCAP_WARP="a,r,x,y,l"    target area,room,x,y,layer (decimal or 0x-hex)
 *   TMC_ROOMCAP_SETTLE=<frames>     frames to settle after warp (default 300)
 *   TMC_ROOMCAP_OUT=<path.png>      output PNG (default roomcap.png)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"            /* gMain, SetTask, TASK_* */
#include "save.h"            /* SaveFile, gSave */
#include "fileselect.h"      /* gMapDataBottomSpecial, ResetSaveFile */
#include "room.h"            /* gRoomControls */
#include "port_repro.h"
#include "port_gba_mem.h"    /* gIoMem */
#include "port_debug_actions.h"

extern int Port_CaptureBaseFramebufferPNG(const char* path);
extern void SetActiveSave(u32 idx); /* fileselect.c (no header decl) */

#define KEYINPUT_REG 0x130
#define START_BUTTON 0x0008

void Port_ReproRoomCap_Tick(unsigned int frame) {
    static int active = -1;
    static int booted = 0, warp_done = 0, cap_frame = 0, settle = 300;
    static unsigned int a = 0x03, r = 0x08, x = 0x1d8, y = 0x138, l = 0;

    if (active < 0) {
        const char* env = getenv("TMC_ROOMCAP");
        active = (env && *env && strcmp(env, "0") != 0) ? 1 : 0;
        if (active) {
            const char* st = getenv("TMC_ROOMCAP_SETTLE");
            if (st && *st) settle = atoi(st);
            const char* w = getenv("TMC_ROOMCAP_WARP");
            if (w && *w) sscanf(w, "%i,%i,%i,%i,%i", &a, &r, &x, &y, &l);
            fprintf(stderr, "[roomcap] active warp=0x%02x/0x%02x (%u,%u) layer=%u settle=%d\n", a, r, x, y, l, settle);
        }
    }
    if (!active)
        return;

    /* Press START at the title to reach file-select. */
    if (!booted && gMain.task == TASK_TITLE && frame >= 30 && (frame & 0xF) < 3) {
        *(volatile unsigned short*)(gIoMem + KEYINPUT_REG) &= ~START_BUTTON;
    }

    /* At file-select, bootstrap a clean new game whose start room is the target,
     * then jump straight into TASK_GAME (skips the intro cutscene entirely). */
    if (!booted && gMain.task == TASK_FILE_SELECT && frame > 60) {
        SaveFile* sv = &gMapDataBottomSpecial.saves[0];
        ResetSaveFile(0);
        sv->initialized = 1;
        sv->name[0] = 'A'; /* non-empty: skip FinalizeSave's default-name copy */
        sv->saved_status.area_next = (u8)a;
        sv->saved_status.room_next = (u8)r;
        sv->saved_status.start_pos_x = (s16)x;
        sv->saved_status.start_pos_y = (s16)y;
        sv->saved_status.layer = (u8)l;
        gMapDataBottomSpecial.saveStatus[0] = 1; /* SAVE_VALID */
        SetActiveSave(0);
        SetTask(TASK_GAME);
        booted = 1;
        fprintf(stderr, "[roomcap] frame %u: bootstrapped new game -> TASK_GAME (start 0x%02x/0x%02x)\n", frame, a, r);
    }

    /* Once stable in TASK_GAME, warp to the target (re-renders from active ROM). */
    if (booted && !warp_done && gMain.task == TASK_GAME && frame % 20 == 0) {
        int rc = Port_DebugAction_Warp((unsigned char)a, (unsigned char)r, (unsigned short)x, (unsigned short)y,
                                       (unsigned char)l);
        fprintf(stderr, "[roomcap] frame %u: warp -> %d (task=%u area=0x%02x room=0x%02x)\n", frame, rc,
                (unsigned)gMain.task, (unsigned)gRoomControls.area, (unsigned)gRoomControls.room);
        if (rc == 1) {
            warp_done = 1;
            cap_frame = (int)(frame + settle);
        }
    }

    if (warp_done && cap_frame && (int)frame >= cap_frame) {
        const char* out = getenv("TMC_ROOMCAP_OUT");
        if (!out || !*out)
            out = "roomcap.png";
        int ok = Port_CaptureBaseFramebufferPNG(out);
        fprintf(stderr, "[roomcap] frame %u: captured %s -> %d (area=0x%02x room=0x%02x)\n", frame, out, ok,
                (unsigned)gRoomControls.area, (unsigned)gRoomControls.room);
        fflush(stderr);
        _Exit(ok ? 0 : 2);
    }

    if (frame == 5000) {
        fprintf(stderr, "[roomcap] timeout (booted=%d task=%u warp=%d)\n", booted, (unsigned)gMain.task, warp_done);
        fflush(stderr);
        _Exit(3);
    }
}
