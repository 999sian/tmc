/*
 * port/port_repro_litarea.c — Issues #138 / #79 auto-reproduction harness.
 *
 * When env var TMC_REPRO_LITAREA is set, drives the game from the title
 * screen into a dark room whose light circles are litArea OBJ-window
 * sprites (src/object/litArea.c), waits for it to settle, writes the base
 * GBA framebuffer to a PNG (TMC_REPRO_LITAREA_OUT, default
 * "litarea_capture.png"), and exits.
 *
 * Target defaults to Temple of Droplets room 0x2C (two light circles) —
 * the "OBJWIN test: ToD Madderpillars" warp in the F8 debug menu. Override
 * with TMC_REPRO_LITAREA="area,room,x,y,layer" (decimal or 0x-hex).
 *
 * Correct render: dark room with soft circular reveals. Broken render
 * (#138/#79): the litArea sprites draw as opaque white blobs because the
 * mode-2 OBJ-window handling is absent/ineffective. The capture is the
 * base framebuffer (what F9 / the issue screenshots show), so it works
 * headless (SDL_VIDEODRIVER=dummy) on Linux and under Wine.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"         /* gMain */
#include "room.h"
#include "port_gba_mem.h" /* gIoMem */

extern int Port_DebugAction_Warp(unsigned char area, unsigned char room, unsigned short x, unsigned short y,
                                 unsigned char layer);
extern int Port_CaptureBaseFramebufferPNG(const char* path); /* shim in port_bugreport.cpp */

#define KEYINPUT_REG 0x130
#define A_BUTTON 0x0001
#define START_BUTTON 0x0008

void Port_ReproLitArea_Tick(unsigned int frame) {
    static int active = -1;
    static int warp_done = 0;
    static int capture_frame = 0;
    static unsigned char last_task = 255;
    static unsigned char tArea = 0x60, tRoom = 0x2C, tLayer = 1;
    static unsigned short tX = 0xBC, tY = 0x58;

    if (active < 0) {
        const char* env = getenv("TMC_REPRO_LITAREA");
        active = (env && *env && strcmp(env, "0") != 0) ? 1 : 0;
        if (active) {
            unsigned int a, r, x, y, l;
            if (sscanf(env, "%i,%i,%i,%i,%i", &a, &r, &x, &y, &l) == 5) {
                tArea = (unsigned char)a;
                tRoom = (unsigned char)r;
                tX = (unsigned short)x;
                tY = (unsigned short)y;
                tLayer = (unsigned char)l;
            }
            fprintf(stderr, "[litarea] harness active; target area=0x%02x room=0x%02x (%u,%u) layer=%u\n", tArea, tRoom,
                    tX, tY, tLayer);
        }
    }
    if (!active)
        return;
    if (gMain.task != last_task) {
        fprintf(stderr, "[litarea] frame %u: task %u -> %u\n", frame, (unsigned)last_task, (unsigned)gMain.task);
        last_task = gMain.task;
    }

    /* Drive title + file-select automatically (GBA KEYINPUT: 0 = pressed).
     * Mash across wide windows so we are robust to per-frame timing
     * differences between native and Wine. */
    {
        unsigned short presses = 0;
        if (gMain.task == 0 && frame >= 30) {
            /* Title: pulse START every 16 frames. */
            if ((frame & 0xF) < 3)
                presses |= START_BUTTON;
        }
        if (gMain.task == 1) {
            /* File-select: pulse A every 16 frames to confirm the slot. */
            if ((frame & 0xF) < 3)
                presses |= A_BUTTON;
        }
        if (presses) {
            *(volatile unsigned short*)(gIoMem + KEYINPUT_REG) &= ~presses;
        }
    }
    /* Once in TASK_GAME (2), retry the warp until it succeeds. */
    if (gMain.task == 2 && !warp_done && (frame % 60 == 0) && frame > 600) {
        int rc = Port_DebugAction_Warp(tArea, tRoom, tX, tY, tLayer);
        fprintf(stderr, "[litarea] frame %u: warp -> %d\n", frame, rc);
        if (rc == 1) {
            warp_done = 1;
            capture_frame = (int)(frame + 300); /* let the room + litArea sprites settle */
        }
    }

    if (warp_done && capture_frame && (int)frame >= capture_frame) {
        const char* out = getenv("TMC_REPRO_LITAREA_OUT");
        if (!out || !*out) {
            out = "litarea_capture.png";
        }
        int ok = Port_CaptureBaseFramebufferPNG(out);
        fprintf(stderr, "[litarea] frame %u: captured %s -> %d\n", frame, out, ok);
        fflush(stderr);
        exit(ok ? 0 : 2);
    }

    if (frame == 6000) {
        fprintf(stderr, "[litarea] timeout (warp_done=%d)\n", warp_done);
        fflush(stderr);
        exit(3);
    }
}
