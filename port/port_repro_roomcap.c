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
#include <errno.h>
#include <ctype.h>
#include "main.h"       /* gMain, SetTask, TASK_* */
#include "save.h"       /* SaveFile, gSave */
#include "flags.h"      /* SetGlobalFlag, LVn_CLEAR, TABIDACHI (TMC_ROOMCAP_PROGRESS) */
#include "fileselect.h" /* gMapDataBottomSpecial, ResetSaveFile */
#include "room.h"       /* gRoomControls */
#include "message.h"    /* gMessage, MessageRequest (TMC_ROOMCAP_MSG hook) */
#include "script.h"     /* gActiveScriptInfo, ScriptExecutionContext (TMC_ROOMCAP_PAN_PROBE) */
#include "port_repro.h"
#include "pauseMenu.h"
#include "fade.h"
#include "port_gba_mem.h" /* gIoMem, gVram, gBgPltt, gObjPltt, gOamMem */
#include "port_debug_actions.h"
#include "port_entity_ctx.h" /* Port_GetEntityScriptCtx (TMC_ROOMCAP_TOUR_DEBUG) */
#include "ui.h"         /* gHUD, HUD_HIDE_ALL (TMC_ROOMCAP_CLEAN) */
#include "game.h"       /* GAMEMAIN_UPDATE (TMC_ROOMCAP_TOUR) */
#include "player.h"     /* gPlayerEntity (TMC_ROOMCAP_CLEAN) */
#include "item_ids.h"
#include "area.h"      /* gAreaRoomHeaders (TMC_ROOMCAP_AUDIT) */   /* ITEM_KINSTONE_BAG (TMC_ROOMCAP_TOUR) */

extern int Port_CaptureBaseFramebufferPNG(const char* path);
extern int Port_CapturePresentationPNG(const char* path);

/* Presentation readback is queued at the end of a frame; poll next tick. */
static int RoomCap_Capture(const char* path) {
    const char* present = getenv("TMC_ROOMCAP_PRESENTATION");
    if (present && *present && strcmp(present, "0") != 0)
        return Port_CapturePresentationPNG(path);
    return Port_CaptureBaseFramebufferPNG(path) ? 1 : -1;
}
/* Parse before narrowing to the warp API's byte/halfword fields. */
static int RoomCap_ParseTourRow(const char* line, unsigned int values[5]) {
    int count = 0;
    while (1) {
        while (isspace((unsigned char)*line))
            ++line;
        if (!*line || *line == '#')
            break;
        if (count == 5)
            return -1;
        char* end;
        errno = 0;
        long value = strtol(line, &end, 0);
        long max = count == 2 || count == 3 ? 65535 : 255;
        if (end == line || errno || value < 0 || value > max ||
            (*end && !isspace((unsigned char)*end) && *end != '#'))
            return -1;
        values[count++] = (unsigned int)value;
        line = end;
    }
    return count == 0 ? 0 : count >= 4 ? 1 : -1;
}

extern void SetActiveSave(u32 idx); /* fileselect.c (no header decl) */

/* Dump the mode1 PPU memory (io/vram/bg-pltt/obj-pltt/oam) for JP-vs-USA diff. */
static void RoomCap_DumpPpu(const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f)
        return;
    const char magic[4] = { 'T', 'M', 'C', 'P' };
    const uint32_t sizes[5] = { 0x400u, 0x18000u, 0x200u, 0x200u, 0x400u };
    fwrite(magic, 1, 4, f);
    fwrite(sizes, sizeof(uint32_t), 5, f);
    fwrite(gIoMem, 1, 0x400u, f);
    fwrite(gVram, 1, 0x18000u, f);
    fwrite(gBgPltt, 1, 0x200u, f);
    fwrite(gObjPltt, 1, 0x200u, f);
    fwrite(gOamMem, 1, 0x400u, f);
    fclose(f);
    fprintf(stderr, "[roomcap] dumped PPU snapshot -> %s (dispcnt=0x%04x)\n", path,
            (unsigned)(gIoMem[0] | (gIoMem[1] << 8)));
}

#define KEYINPUT_REG 0x130
#define START_BUTTON 0x0008

void Port_ReproRoomCap_Tick(unsigned int frame) {
    static int active = -1;
    static int booted = 0, warp_done = 0, cap_frame = 0, settle = 300;
    static int tour_running = 0;
    static int msg_posY = -1;
    static unsigned int a = 0x03, r = 0x08, x = 0x1d8, y = 0x138, l = 0;

    if (active < 0) {
        const char* env = getenv("TMC_ROOMCAP");
        active = (env && *env && strcmp(env, "0") != 0) ? 1 : 0;
        if (active) {
            const char* st = getenv("TMC_ROOMCAP_SETTLE");
            if (st && *st)
                settle = atoi(st);
            const char* w = getenv("TMC_ROOMCAP_WARP");
            if (w && *w)
                sscanf(w, "%i,%i,%i,%i,%i", &a, &r, &x, &y, &l);
            fprintf(stderr, "[roomcap] active warp=0x%02x/0x%02x (%u,%u) layer=%u settle=%d\n", a, r, x, y, l, settle);
        }
    }
    if (!active)
        return;
    if (frame >= 5000 && !(booted && tour_running)) {
        fprintf(stderr, "[roomcap] timeout (task=%u)\n", (unsigned)gMain.task);
        fflush(stderr);
        _Exit(3);
    }

    /* Press START at the title to reach file-select. */
    if (!booted && gMain.task == TASK_TITLE && frame >= 30 && (frame & 0xF) < 3) {
        *(volatile unsigned short*)(gIoMem + KEYINPUT_REG) &= ~START_BUTTON;
    }

    /* FILE-SELECT animation probe: sit at file-select and capture two frames a
     * fixed distance apart so the caller can diff them. Does NOT bootstrap a
     * save / enter TASK_GAME. Enable with TMC_ROOMCAP_FILESELECT=1.
     * Outputs <OUT base>_a.png and <OUT base>_b.png. */
    {
        static int fsel = -1;
        static int fsel_first = 0;
        static unsigned fsel_ready = 0;
        if (fsel < 0) {
            const char* fe = getenv("TMC_ROOMCAP_FILESELECT");
            fsel = (fe && *fe && strcmp(fe, "0") != 0) ? 1 : 0;
        }
        if (fsel) {
            extern int Port_FselAnimTrace;
            unsigned dispcnt = (unsigned)(gIoMem[0] | (gIoMem[1] << 8));
            /* Wait until file-select is fully presented: OBJ layer on (bit 12). */
            int objon = (dispcnt & 0x1000) != 0;
            /* Anim trace: once OBJ is on and stable, trace UpdateAnimationSingleFrame
             * for ~3 frames, then dump call counter. */
            if (getenv("TMC_ROOMCAP_FSEL_ANIMTRACE") && gMain.task == TASK_FILE_SELECT && frame > 200 && objon) {
                static int at_first = 0;
                extern unsigned long Port_UASF_Calls;
                if (at_first == 0) {
                    at_first = (int)frame;
                    fprintf(stderr, "[fselcnt] frame %u objon: UASF calls so far = %lu (state=%u lastState=%u)\n",
                            frame, Port_UASF_Calls, (unsigned)gMain.state, (unsigned)gUI.lastState);
                }
                if ((int)frame >= at_first && (int)frame <= at_first + 2) {
                    Port_FselAnimTrace = 1;
                } else {
                    Port_FselAnimTrace = 0;
                }
                if ((int)frame == at_first + 4) {
                    fprintf(stderr, "[fselcnt] frame %u: UASF calls now = %lu\n", frame, Port_UASF_Calls);
                    fflush(stderr);
                    _Exit(0);
                }
            }
            /* Time-series mode: dump OAM every 4 frames for 12 samples. */
            if (getenv("TMC_ROOMCAP_FSEL_SERIES") && gMain.task == TASK_FILE_SELECT && frame > 200 && objon) {
                static int s_n = 0;
                static unsigned s_last = 0;
                if (s_n < 12 && (s_last == 0 || frame >= s_last + 4)) {
                    char d[512];
                    const char* dp = getenv("TMC_ROOMCAP_DUMP");
                    if (!dp || !*dp)
                        dp = "fsel_series";
                    snprintf(d, sizeof(d), "%s_%02d.bin", dp, s_n);
                    RoomCap_DumpPpu(d);
                    s_last = frame;
                    s_n++;
                    if (s_n >= 12) {
                        fflush(stderr);
                        _Exit(0);
                    }
                }
                return;
            }
            if (gMain.task == TASK_FILE_SELECT && frame > 200 && objon && !gFadeControl.active) {
                if (fsel_ready == 0)
                    fsel_ready = frame;
                /* The base framebuffer is from the preceding frame; let the fade finish. */
                if (frame < fsel_ready + 30)
                    return;
                const char* base = getenv("TMC_ROOMCAP_OUT");
                if (!base || !*base)
                    base = "fsel.png";
                if (fsel_first == 0) {
                    char p[512];
                    snprintf(p, sizeof(p), "%s_a.png", base);
                    int ok = RoomCap_Capture(p);
                    if (ok == 0)
                        return;
                    if (ok < 0)
                        _Exit(2);
                    fprintf(stderr, "[roomcap-fsel] frame %u: captured %s -> %d (state=%u substate=%u)\n", frame, p, ok,
                            (unsigned)gMain.state, (unsigned)gUI.state);
                    fsel_first = (int)frame;
                    const char* dumpa = getenv("TMC_ROOMCAP_DUMP");
                    if (dumpa && *dumpa) {
                        char d[512];
                        snprintf(d, sizeof(d), "%s_a.bin", dumpa);
                        RoomCap_DumpPpu(d);
                    }
                } else if ((int)frame >= fsel_first + 60) {
                    char p[512];
                    snprintf(p, sizeof(p), "%s_b.png", base);
                    int ok = RoomCap_Capture(p);
                    if (ok == 0)
                        return;
                    if (ok < 0)
                        _Exit(2);
                    fprintf(stderr, "[roomcap-fsel] frame %u: captured %s -> %d\n", frame, p, ok);
                    const char* dumpb = getenv("TMC_ROOMCAP_DUMP");
                    if (dumpb && *dumpb) {
                        char d[512];
                        snprintf(d, sizeof(d), "%s_b.bin", dumpb);
                        RoomCap_DumpPpu(d);
                    }
                    fflush(stderr);
                    _Exit(ok > 0 ? 0 : 2);
                }
            }
            return; /* don't fall through to the room-bootstrap path */
        }
    }

    /* At file-select, bootstrap a clean new game whose start room is the target,
     * then jump straight into TASK_GAME (skips the intro cutscene entirely). */
    if (!booted && gMain.task == TASK_FILE_SELECT && frame > 60) {
        SaveFile* sv = &gFileSelectState.saves[0];
        ResetSaveFile(0);
        sv->initialized = 1;
        sv->name[0] = 'A'; /* non-empty: skip FinalizeSave's default-name copy */
        sv->saved_status.area_next = (u8)a;
        sv->saved_status.room_next = (u8)r;
        sv->saved_status.start_pos_x = (s16)x;
        sv->saved_status.start_pos_y = (s16)y;
        sv->saved_status.layer = (u8)l;
        gFileSelectState.saveStatus[0] = 1; /* SAVE_VALID */
        SetActiveSave(0);
        SetTask(TASK_GAME);
        booted = 1;
        fprintf(stderr, "[roomcap] frame %u: bootstrapped new game -> TASK_GAME (start 0x%02x/0x%02x)\n", frame, a, r);
    }

    /* TMC_ROOMCAP_AUDIT=<all|area,area,...>: edge-scroll transition audit.
     * One session walks Link (noclip, no damage) out of every room edge
     * that borders another room of the same area, and logs each roll:
     *   [audit] J=<job> a= r= d= along= dest= res=<ok|blocked|exit|warpfail|stuck>
     *           void=<max px outside both rooms> voidcols= rev= wchg= jump= step=
     *           S=<x,y,w,h> D=<x,y,w,h>
     * plus [audit-cap] lines naming each capture (TMC_ROOMCAP_AUDIT_OUT/
     * jNNNNN_<tag>.png) with the camera it was taken at, for offline image
     * checks. TMC_ROOMCAP_AUDIT_START resumes at a job; TMC_ROOMCAP_AUDIT_EXITS=1
     * also walks edges with no neighbor room (area-change exits). */
    {
        typedef struct {
            unsigned char area, room, dir, dest;
            unsigned short along, lo, hi; /* run of the edge facing dest */
        } AuJob;
        static int au = -1, nj = 0, ji = 0, st = 0;
        static AuJob jobs[8192];
        static unsigned int fired = 0, since = 0, mark = 0;
        static int lsx, lsy, stable, rwid, rlast, rstep, rfail_rev, rfail_w, vmax, vcols, lastcap, forced;
        static int pcx, pcy, pcw, ax, ay;
        static int S[4], D[4];
        if (au < 0) {
            const char* spec = getenv("TMC_ROOMCAP_AUDIT");
            au = 0;
            if (spec && *spec) {
                const int exits = getenv("TMC_ROOMCAP_AUDIT_EXITS") != NULL;
                unsigned char want[256] = { 0 };
                extern int Port_DebugAction_AreaIsWarpable(unsigned char area);
                if (strcmp(spec, "all") == 0) {
                    for (int a = 0; a < 0x90; a++)
                        want[a] = Port_DebugAction_AreaIsWarpable((unsigned char)a) != 0;
                } else {
                    const char* p = spec;
                    while (*p) {
                        char* end;
                        long a = strtol(p, &end, 0);
                        if (end == p)
                            break;
                        if (a >= 0 && a < 256)
                            want[a] = 1;
                        p = *end ? end + 1 : end;
                    }
                }
                for (int a = 0; a < 256; a++) {
                    if (!want[a])
                        continue;
                    extern bool32 Port_IsRoomHeaderPtrReadable(const void* ptr);
                    extern void Port_RefreshAreaData(u32 area);
                    RoomHeader* t = gAreaRoomHeaders[a];
                    if (!Port_IsRoomHeaderPtrReadable(t)) {
                        Port_RefreshAreaData((u32)a);
                        t = gAreaRoomHeaders[a];
                    }
                    if (!Port_IsRoomHeaderPtrReadable(t))
                        continue;
                    int n = 0;
                    while (n < 64 && Port_IsRoomHeaderPtrReadable(t + n) && t[n].map_x != 0xFFFF)
                        n++;
                    for (int r = 0; r < n; r++) {
                        const RoomHeader* h = &t[r];
                        if (h->tileSet_id == 0xFFFF || h->pixel_width == 0 || h->pixel_height == 0)
                            continue;
                        for (int d = 0; d < 4; d++) {
                            int span = (d & 1) ? h->pixel_height : h->pixel_width;
                            int runStart = -1, runDest = -1;
                            for (int along = 8; along <= span - 8 + 8; along += 8) {
                                int nb = -1;
                                if (along <= span - 8) {
                                    int px = d == 1 ? h->map_x + h->pixel_width + 16 : d == 3 ? h->map_x - 16 : h->map_x + along;
                                    int py = d == 0 ? h->map_y - 16 : d == 2 ? h->map_y + h->pixel_height + 16 : h->map_y + along;
                                    nb = 0xFF;
                                    for (int q = 0; q < n; q++) {
                                        const RoomHeader* o = &t[q];
                                        if (q == r || o->tileSet_id == 0xFFFF)
                                            continue;
                                        if ((u32)(px - o->map_x) < o->pixel_width && (u32)(py - o->map_y) < o->pixel_height) {
                                            nb = q;
                                            break;
                                        }
                                    }
                                }
                                if (nb != runDest) {
                                    if (runStart >= 0 && (runDest != 0xFF || exits) && nj < 8192) {
                                        AuJob* j = &jobs[nj++];
                                        j->area = (unsigned char)a, j->room = (unsigned char)r, j->dir = (unsigned char)d;
                                        j->dest = (unsigned char)runDest;
                                        j->along = (unsigned short)((runStart + along - 8) / 2);
                                        j->lo = (unsigned short)runStart, j->hi = (unsigned short)(along - 8);
                                    }
                                    runStart = along, runDest = nb;
                                }
                            }
                        }
                    }
                }
                const char* s0 = getenv("TMC_ROOMCAP_AUDIT_START");
                ji = s0 && *s0 ? atoi(s0) : 0;
                au = tour_running = 1;
                fprintf(stderr, "[audit] %d jobs, from %d\n", nj, ji);
            }
        }
        if (au) {
            extern int Port_Widescreen_EffectiveViewWidth(void);
            extern int Port_Widescreen_LatchedFrameWidth(void);
            extern void Port_Config_SetWidescreenEnabled(bool enabled);
            static const unsigned short kKeys[] = { DPAD_UP, DPAD_RIGHT, DPAD_DOWN, DPAD_LEFT };
            if (!booted || gMain.task != TASK_GAME)
                return;
            if (ji >= nj) {
                fprintf(stderr, "[audit] done\n");
                fflush(stderr);
                _Exit(0);
            }
            const AuJob* j = &jobs[ji];
            const char* dir = getenv("TMC_ROOMCAP_AUDIT_OUT");
            char path[512];
            Port_Config_SetWidescreenEnabled(true);
            Port_DebugAction_SetNoclip(1);
            Port_DebugAction_SetNoDamage(1);
            gHUD.hideFlags = HUD_HIDE_ALL;
            gSave.stats.health = gSave.stats.maxHealth;
            const int calm = gPlayerState.controlMode == CONTROL_ENABLED && !(gMessage.state & MESSAGE_ACTIVE);
            if (!calm && st != 5 && frame % 40 < 2) {
                extern void Port_Config_TestForceEdge(int input);
                Port_Config_TestForceEdge(0 /* PORT_INPUT_A */);
            }
            if (since == 0)
                since = frame;
            const int vw = Port_Widescreen_LatchedFrameWidth();
            const int sx = gRoomControls.scroll_x, sy = gRoomControls.scroll_y;
#define AU_CAP(tag)                                                                                                    \
    do {                                                                                                               \
        snprintf(path, sizeof path, "%s/j%05d_%s.png", dir && *dir ? dir : ".", ji, tag);                              \
        if (RoomCap_Capture(path) > 0)                                                                                 \
            fprintf(stderr, "[audit-cap] J=%d tag=%s cam=%d,%d,%d prev=%d,%d,%d\n", ji, tag, sx, sy, vw, pcx, pcy,    \
                    pcw);                                                                                              \
    } while (0)
#define AU_NEXT(res, jump)                                                                                             \
    do {                                                                                                               \
        fprintf(stderr,                                                                                                \
                "[audit] J=%d a=%02x r=%02x d=%d along=%d,%d dest=%02x res=%s void=%d voidcols=%d rev=%d wchg=%d "       \
                "jump=%d step=%d S=%d,%d,%d,%d D=%d,%d,%d,%d link=%d,%d st=%d ps=%d forced=%d\n",                                                       \
                ji, j->area, j->room, j->dir, ax, ay, j->dest, res, vmax, vcols, rfail_rev, rfail_w, jump, rstep, \
                S[0], S[1], S[2], S[3], D[0], D[1], D[2], D[3], gPlayerEntity.base.x.HALF.HI - gRoomControls.origin_x, gPlayerEntity.base.y.HALF.HI - gRoomControls.origin_y, st, gPlayerState.framestate, forced);                                                       \
        fflush(stderr);                                                                                                \
        ++ji, st = 0, since = 0, fired = 0, forced = 0;                                                                            \
    } while (0)
            if (st != 5 && st != 6 && frame - since > 1200) {
                AU_NEXT(st <= 1 ? "warpfail" : "stuck", 0);
                return;
            }
            const RoomHeader* hs = &gAreaRoomHeaders[j->area][j->room];
            const int lx = (j->dir & 1) ? (j->dir == 1 ? hs->pixel_width - 5 : 5) : j->along;
            const int ly = (j->dir & 1) ? j->along : (j->dir == 2 ? hs->pixel_height - 5 : 5);
            (void)ax, (void)ay;
            switch (st) {
                case 0:
                    vmax = vcols = rfail_rev = rfail_w = rstep = 0, rwid = -1, lastcap = -100, ax = ay = -1;
                    memset(S, 0, sizeof S), memset(D, 0, sizeof D);
                    if (gRoomControls.area == j->area && gRoomControls.room == j->room && calm &&
                        gRoomControls.camera_target == &gPlayerEntity.base) {
                        st = 2, mark = frame; /* already here */
                    } else if ((fired == 0 || frame - fired > 180) &&
                               Port_DebugAction_Warp(j->area, j->room, (unsigned short)lx, (unsigned short)ly, 1) == 1) {
                        fired = frame, st = 1;
                    }
                    break;
                case 1:
                    if (gRoomControls.area == j->area && gRoomControls.room == j->room && calm &&
                        (gMain.substate == GAMEMAIN_UPDATE || gMain.substate == GAMEMAIN_BARRELUPDATE) &&
                        gRoomControls.camera_target == &gPlayerEntity.base && frame - fired > 20) {
                        st = 2, mark = frame;
                    } else if (frame - fired > 180) {
                        st = 0;
                    }
                    break;
                case 2:
                    if (frame - mark < 10)
                        break;
                    {
                        /* The edge only lets Link out where the tile past him
                         * reads collision 255 (a doorway; beanstalkSubtask.c's test).
                         * Take the middle of
                         * the longest such stretch facing dest, on either layer. */
                        extern u32 GetCollisionDataAtTilePos(u32 tilePos, u32 layer);
                        int best = -1, bestLen = 0, bestLayer = 1;
                        for (int layer = 1; layer <= 2; layer++) {
                            int run0 = -1;
                            for (int a = j->lo; a <= j->hi + 4; a += 4) {
                                int ok = 0;
                                if (a <= j->hi) {
                                    /* the border tile just past the edge */
                                    int px = gRoomControls.origin_x +
                                             ((j->dir & 1) ? (j->dir == 1 ? gRoomControls.width : -1) : a);
                                    int py = gRoomControls.origin_y +
                                             ((j->dir & 1) ? a : (j->dir == 2 ? gRoomControls.height : -1));
                                    ok = GetCollisionDataAtTilePos(TILE(px, py), (u32)layer) == 255;
                                }
                                if (ok && run0 < 0)
                                    run0 = a;
                                if (!ok && run0 >= 0) {
                                    if (a - run0 > bestLen)
                                        bestLen = a - run0, best = (run0 + a - 4) / 2, bestLayer = layer;
                                    run0 = -1;
                                }
                            }
                        }
                        if (best < 0) {
                            AU_NEXT("wall", 0);
                            return;
                        }
                        ax = (j->dir & 1) ? lx : best;
                        ay = (j->dir & 1) ? best : ly;
                        gPlayerEntity.base.collisionLayer = (u8)bestLayer;
                        gPlayerEntity.base.x.HALF.HI = (s16)(gRoomControls.origin_x + ax);
                        gPlayerEntity.base.y.HALF.HI = (s16)(gRoomControls.origin_y + ay);
                    }
                    st = 3, stable = 0, mark = frame;
                    break;
                case 3:
                    stable = (sx == lsx && sy == lsy) ? stable + 1 : 0;
                    if (stable >= 4 || frame - mark > 150) {
                        AU_CAP("pre");
                        st = 4, mark = frame;
                    }
                    break;
                case 4:
                    *(volatile unsigned short*)(gIoMem + KEYINPUT_REG) &= ~kKeys[j->dir];
                    if (gRoomControls.scrollAction == 2) {
                        const RoomHeader* hd = j->dest != 0xFF ? &gAreaRoomHeaders[j->area][j->dest] : NULL;
                        S[0] = hs->map_x, S[1] = hs->map_y, S[2] = hs->pixel_width, S[3] = hs->pixel_height;
                        if (hd)
                            D[0] = hd->map_x, D[1] = hd->map_y, D[2] = hd->pixel_width, D[3] = hd->pixel_height;
                        rlast = (j->dir & 1) ? sx + Port_Widescreen_EffectiveViewWidth() / 2 : sy;
                        st = 5, mark = frame;
                    } else if (gRoomControls.area != j->area || gRoomControls.room != j->room || gFadeControl.active) {
                        AU_NEXT("exit", 0);
                        return;
                    } else if (frame - mark > 30 && j->dest != 0xFF && !forced) {
                        /* Walking out failed (trees, bushes, story gates in
                         * this save). Start the roll the way the edge check
                         * does, from the doorway, so the render is audited. */
                        gPlayerEntity.base.x.HALF.HI = (s16)(gRoomControls.origin_x + ax);
                        gPlayerEntity.base.y.HALF.HI = (s16)(gRoomControls.origin_y + ay);
                        gPlayerEntity.base.direction = (u8)(j->dir << 3);
                        if (sub_0807BD14(&gPlayerEntity.base, j->dir))
                            forced = 1, gPlayerEntity.base.direction = (u8)(j->dir << 3);
                        else
                            forced = 2;
                    } else if (frame - mark > 60) {
                        AU_NEXT("blocked", 0);
                        return;
                    }
                    break;
                case 5: {
                    if (gRoomControls.scrollAction != 2) {
                        st = 6, mark = frame;
                        break;
                    }
                    const int pos = (j->dir & 1) ? sx + Port_Widescreen_EffectiveViewWidth() / 2 : sy;
                    if (gRoomControls.unk_18 >= 1) {
                        if (rwid < 0)
                            rwid = vw;
                        if (vw != rwid)
                            rfail_w = 1;
                    }
                    if ((j->dir == 1 || j->dir == 2) ? pos < rlast : pos > rlast)
                        rfail_rev = 1;
                    if (gRoomControls.unk_18 >= 2 && abs(pos - rlast) > rstep)
                        rstep = abs(pos - rlast);
                    rlast = pos;
                    /* view area outside both rooms, at the width this
                     * frame presents (the latch lags the camera a tick) */
                    {
                        const int vw = Port_Widescreen_EffectiveViewWidth();
                        long cover = 0;
                        int cols = 0;
                        const int* R[2] = { S, D };
                        for (int k = 0; k < 2; k++) {
                            int x0 = sx > R[k][0] ? sx : R[k][0], x1 = sx + vw < R[k][0] + R[k][2] ? sx + vw : R[k][0] + R[k][2];
                            int y0 = sy > R[k][1] ? sy : R[k][1], y1 = sy + 160 < R[k][1] + R[k][3] ? sy + 160 : R[k][1] + R[k][3];
                            if (x1 > x0 && y1 > y0)
                                cover += (long)(x1 - x0) * (y1 - y0);
                        }
                        for (int c = 0; c < vw; c++) {
                            int wx = sx + c, in = 0;
                            for (int k = 0; k < 2; k++)
                                in |= wx >= R[k][0] && wx < R[k][0] + R[k][2];
                            cols += !in;
                        }
                        int v = (int)((long)vw * 160 - cover);
                        if (v > vmax)
                            vmax = v;
                        if (cols > vcols)
                            vcols = cols;
                    }
                    if (gRoomControls.unk_18 >= lastcap + 12) {
                        char tag[16];
                        snprintf(tag, sizeof tag, "s%02d", (int)gRoomControls.unk_18);
                        AU_CAP(tag);
                        lastcap = gRoomControls.unk_18;
                    }
                    break;
                }
                case 6: {
                    if (frame == mark + 0) {
                        /* nothing: first follow frame is checked below */
                    }
                    const int pos = (j->dir & 1) ? sx + Port_Widescreen_EffectiveViewWidth() / 2 : sy;
                    static int jump;
                    if (frame == mark)
                        jump = pos - rlast;
                    if (frame - mark >= 40) {
                        AU_CAP("post");
                        AU_NEXT(abs(jump) > rstep + 1 ? "jump" : "ok", jump);
                        return;
                    }
                    break;
                }
            }
            lsx = sx, lsy = sy;
            pcx = sx, pcy = sy, pcw = vw;
#undef AU_CAP
#undef AU_NEXT
            return;
        }
    }

    /* TMC_ROOMCAP_TOUR=<plan>: capture many views in one session, as the
     * voxel tour (port_repro_npc_talk.c) walks every room -- a restart per
     * capture costs seconds; this costs the warp. The plan is one view a
     * line, "area room x y [layer]" (x, y in the room's own pixels); the
     * captures go to TMC_ROOMCAP_TOUR_OUT/<line>.png, each logged as
     * "[roomcap] tour <line> view room=../.. scroll=x,y size=w,h" (the
     * camera in room pixels). Clean throughout (TMC_ROOMCAP_CLEAN). A view
     * that never lands in its room fails the tour so drivers can resume. */
    {
        static int tour = -1, tn = 0, ti = 0;
        static unsigned char ta[4096], tr[4096], tl[4096];
        static unsigned short tx[4096], ty[4096];
        static unsigned int fired = 0, arrived = 0, since = 0;
        if (tour < 0) {
            const char* plan = getenv("TMC_ROOMCAP_TOUR");
            tour = 0;
            if (plan && *plan) {
                FILE* f = fopen(plan, "r");
                if (!f) {
                    fprintf(stderr, "[roomcap] cannot read tour plan %s\n", plan);
                    _Exit(2);
                }
                char line[256];
                while (fgets(line, sizeof line, f)) {
                    unsigned int values[5] = { 0 };
                    int row = RoomCap_ParseTourRow(line, values);
                    if (row < 0 || (!strchr(line, '\n') && !feof(f)) || (row && tn == 4096)) {
                        fprintf(stderr, "[roomcap] invalid or oversized tour plan %s\n", plan);
                        _Exit(2);
                    }
                    if (row) {
                        ta[tn] = values[0], tr[tn] = values[1], tl[tn] = values[4];
                        tx[tn] = values[2], ty[tn] = values[3];
                        ++tn;
                    }
                }
                int valid = !ferror(f) && tn > 0;
                if (fclose(f) != 0)
                    valid = 0;
                if (!valid) {
                    fprintf(stderr, "[roomcap] empty or unreadable tour plan %s\n", plan);
                    _Exit(2);
                }
                const char* st = getenv("TMC_ROOMCAP_TOUR_START");
                if (st && *st) {
                    char* end;
                    errno = 0;
                    long start = strtol(st, &end, 10);
                    int parsed = end != st;
                    while (isspace((unsigned char)*end))
                        ++end;
                    if (!parsed || *end || errno || start < 0 || start > tn) {
                        fprintf(stderr, "[roomcap] invalid tour start %s (expected 0..%d)\n", st, tn);
                        _Exit(2);
                    }
                    ti = (int)start;
                }
                tour = tour_running = 1;
                fprintf(stderr, "[roomcap] tour: %d views, from %d\n", tn, ti);
            }
        }
        if (tour) {
            if (ti >= tn) {
                fprintf(stderr, "[roomcap] tour done\n");
                fflush(stderr);
                _Exit(0);
            }
            /* stuck -- a scene or menu that never gives the game back: say
             * which view, and end; the driver starts again after it
             * (TMC_ROOMCAP_TOUR_START) */
            if (booted && since && frame - since > 2400) {
                fprintf(stderr, "[roomcap] tour %d stuck room=0x%02x/0x%02x (task=%u)\n", ti, ta[ti], tr[ti],
                        (unsigned)gMain.task);
                fflush(stderr);
                _Exit(3);
            }
            if (booted && since == 0)
                since = frame;
            if (!booted || gMain.task != TASK_GAME)
                return;
            /* past the prologue (TMC_ROOMCAP_PROGRESS dungeons cleared):
             * without it Hyrule Town redirects to Festival Town (roomInit.c)
             * and never arrives */
            {
                static int progressed = 0;
                if (!progressed) {
                    progressed = 1;
                    const char* pg = getenv("TMC_ROOMCAP_PROGRESS");
                    if (pg && *pg) {
                        int np = atoi(pg);
                        if (np < 0) np = 0;
                        if (np > 6) np = 6;
                        static const u32 kLv[6] = { LV1_CLEAR, LV2_CLEAR, LV3_CLEAR,
                                                    LV4_CLEAR, LV5_CLEAR, LV6_CLEAR };
                        SetGlobalFlag(TABIDACHI);
                        for (int i = 0; i < np; i++)
                            SetGlobalFlag(kLv[i]);
                        /* and what the game hands out by then: a cleared
                         * dungeon's flag is taken back on the next area
                         * change without its element (gameUtils.c, LVn_CLEAR
                         * vs ITEM_*_ELEMENT) -- back to the prologue's town,
                         * whose intro scene holds the camera; without the
                         * kinstone bag, the town plays Ezlo's kinstone scene
                         * (gUnk_080EE88C, roomInit.c) */
                        static const u8 kElem[6] = { ITEM_EARTH_ELEMENT, ITEM_FIRE_ELEMENT, 0,
                                                     ITEM_WATER_ELEMENT, ITEM_WIND_ELEMENT, 0 };
                        for (int i = 0; i < np; i++)
                            if (kElem[i])
                                SetInventoryValue(kElem[i], 1);
                        if (np > 0)
                            SetInventoryValue(ITEM_KINSTONE_BAG, 1);
                        UpdateGlobalProgress();
                        fprintf(stderr, "[roomcap] tour progress: %d dungeons, global_progress=%u\n", np,
                                (unsigned)gSave.global_progress);
                    }
                }
            }
            gHUD.hideFlags = HUD_HIDE_ALL;
            gPlayerEntity.base.spriteSettings.draw = 0;
            gSave.stats.health = gSave.stats.maxHealth;
            const int calm = gPlayerState.controlMode == CONTROL_ENABLED && !(gMessage.state & MESSAGE_ACTIVE);
            if (!calm && frame % 40 < 2) {
                extern void Port_Config_TestForceEdge(int input);
                Port_Config_TestForceEdge(0 /* PORT_INPUT_A */);
            }
            if (since == 0)
                since = frame;
            /* a scene holding the camera on something else is not the view */
            const int here = arrived && gRoomControls.area == ta[ti] && gRoomControls.room == tr[ti] &&
                             (gMain.substate == GAMEMAIN_UPDATE || gMain.substate == GAMEMAIN_BARRELUPDATE) &&
                             gRoomControls.camera_target == &gPlayerEntity.base;
            /* TMC_ROOMCAP_TOUR_DEBUG: each second, the wait -- and the
             * scripts the NPCs run (ROM addresses; port_script_addrs.c
             * names them), to find a scene that holds the camera */
            if (getenv("TMC_ROOMCAP_TOUR_DEBUG") && frame % 60 == 0) {
                fprintf(stderr, "[roomcap] tour %d wait frame %u room=0x%02x/0x%02x sub=%u ctl=%u cam=%d gp=%u\n", ti,
                        frame, (unsigned)gRoomControls.area, (unsigned)gRoomControls.room,
                        (unsigned)gMain.substate, (unsigned)gPlayerState.controlMode,
                        gRoomControls.camera_target ? (int)gRoomControls.camera_target->id : -1,
                        (unsigned)gSave.global_progress);
                for (Entity* e = gEntityLists[7].first; e && e != (Entity*)&gEntityLists[7]; e = e->next) {
                    ScriptExecutionContext* c = (e->flags & ENT_SCRIPTED) ? Port_GetEntityScriptCtx(e) : NULL;
                    if (c && c->scriptInstructionPointer)
                        fprintf(stderr, "[roomcap]   npc %d script at 0x%08x\n", (int)e->id,
                                (unsigned)(0x08000000u + (u32)((u8*)c->scriptInstructionPointer - gRomData)));
                }
            }
            /* a scene on arrival (an item Link lands on: "You got a Piece
             * of Heart!") -- the settle starts again once it is over */
            if (arrived && !calm)
                arrived = frame;
            if (!arrived) {
                /* warp (again, if a fade swallowed it: after 3s) */
                if ((fired == 0 || frame - fired > 180) &&
                    Port_DebugAction_Warp(ta[ti], tr[ti], tx[ti], ty[ti], tl[ti]) == 1) {
                    fired = frame;
                    if (getenv("TMC_ROOMCAP_TOUR_DEBUG"))
                        fprintf(stderr, "[roomcap] tour %d warp fired frame %u\n", ti, frame);
                }
                if (fired && frame - fired > 20 && gRoomControls.area == ta[ti] && gRoomControls.room == tr[ti] &&
                    (gMain.substate == GAMEMAIN_UPDATE || gMain.substate == GAMEMAIN_BARRELUPDATE))
                    arrived = frame;
            } else if (here && (int)(frame - arrived) >= settle) {
                const char* dir = getenv("TMC_ROOMCAP_TOUR_OUT");
                char path[512];
                if ((size_t)snprintf(path, sizeof path, "%s/%05d.png", dir && *dir ? dir : ".", ti) >= sizeof path) {
                    fprintf(stderr, "[roomcap] tour output path too long\n");
                    _Exit(2);
                }
                int ok = RoomCap_Capture(path);
                if (ok < 0) {
                    fprintf(stderr, "[roomcap] tour %d capture failed: %s\n", ti, path);
                    _Exit(2);
                }
                if (ok > 0) {
                    fprintf(stderr, "[roomcap] tour %d view room=0x%02x/0x%02x scroll=%d,%d size=%d,%d ok=%d link=%d,%d\n", ti,
                            (unsigned)gRoomControls.area, (unsigned)gRoomControls.room,
                            (int)(gRoomControls.scroll_x - gRoomControls.origin_x),
                            (int)(gRoomControls.scroll_y - gRoomControls.origin_y), (int)gRoomControls.width,
                            (int)gRoomControls.height, ok,
                            (int)(gPlayerEntity.base.x.HALF.HI - gRoomControls.origin_x),
                            (int)(gPlayerEntity.base.y.HALF.HI - gRoomControls.origin_y));
                    ++ti, fired = 0, arrived = 0, since = 0;
                }
            } else if (!here && gRoomControls.camera_target == &gPlayerEntity.base) {
                arrived = 0;            /* left the room (an exit, a script): warp again;
                                         * a scene holding the camera runs to the timeout */
            }
            if (since && frame - since > 900 && ti < tn) {
                fprintf(stderr, "[roomcap] tour %d failed room=0x%02x/0x%02x (view timeout)\n", ti, ta[ti], tr[ti]);
                fflush(stderr);
                _Exit(3);
            }
            return;
        }
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

    /* ---- TMC_ROOMCAP_COF_PROBE: issue #159 repro -----------------------
     * After the warp settles: set the puzzle's local flags (fight done 0x16,
     * knob seated 0x17), warp again to reload the room so roomInit/object
     * init paths observe them, then probe the seated-chest interaction:
     * teleport Link under the seat, face north, press R, and log the tile
     * type + chest flag 0x18 + inventory small-key count each step. */
    {
        static int probe = -1;
        static int stage = 0, stage_frame = 0;
        if (probe < 0)
            probe = (getenv("TMC_ROOMCAP_COF_PROBE") != NULL) ? 1 : 0;
        if (probe && warp_done) {
            extern void SetLocalFlag(u32 flag);
            extern u32 CheckLocalFlag(u32 flag);
            extern u32 GetTileTypeAtTilePos(u32 position, u32 layer);
            extern u8 GetDungeonState(void);
            /* seat = world (0x108,0x38) -> tilePos 0xd0, layer 2 per room data */
            const u32 seatPos = 0xd0;
            const u32 seatLayer = 2;
            switch (stage) {
                case 0: /* wait for settle, then set flags + reload room */
                    if ((int)frame >= cap_frame - settle + 120) {
                        SetLocalFlag(0x16);
                        SetLocalFlag(0x17);
                        fprintf(stderr, "[cof-probe] frame %u: set flags 0x16/0x17 (chest 0x18=%u), re-warping\n",
                                frame, CheckLocalFlag(0x18));
                        stage = 1;
                    }
                    break;
                case 1: /* re-warp to reload the room with flags set */
                    if (frame % 20 == 0) {
                        int rc = Port_DebugAction_Warp((unsigned char)a, (unsigned char)r, (unsigned short)x,
                                                       (unsigned short)y, (unsigned char)l);
                        if (rc == 1) {
                            stage = 2;
                            stage_frame = (int)frame + 180;
                        }
                    }
                    break;
                case 2: /* room reloaded: log tile state, teleport under seat */
                    if ((int)frame >= stage_frame) {
                        u32 tt_bot = GetTileTypeAtTilePos(seatPos, 1);
                        u32 tt_top = GetTileTypeAtTilePos(seatPos, 2);
                        fprintf(
                            stderr,
                            "[cof-probe] frame %u: post-reload seat tile bottom=0x%x top=0x%x flag17=%u flag18=%u\n",
                            frame, tt_bot, tt_top, CheckLocalFlag(0x17), CheckLocalFlag(0x18));
                        /* stand ON the tile south of the seat (seat y=0x38,
                         * so stand at y=0x48), facing north — the facing
                         * offset then lands exactly on the seat tile */
                        Port_DebugAction_TeleportXY((unsigned short)(gRoomControls.origin_x + 0x108),
                                                    (unsigned short)(gRoomControls.origin_y + 0x48));
                        gPlayerEntity.base.animationState = 0; /* north */
                        gPlayerEntity.base.collisionLayer = (unsigned char)seatLayer;
                        stage = 3;
                        stage_frame = (int)frame + 30;
                    }
                    break;
                case 3: /* press R for a few frames, pinning layer + facing */
                    if ((int)frame >= stage_frame) {
                        extern void Port_Config_TestForceEdge(int input);
                        extern u32 GetActTileForTileType(u32 tileType);
                        gPlayerEntity.base.collisionLayer = (unsigned char)seatLayer;
                        gPlayerEntity.base.animationState = 0;
                        if ((int)frame == stage_frame) {
                            u32 px = (u32)(u16)(gPlayerEntity.base.x.HALF.HI - gRoomControls.origin_x);
                            u32 py = (u32)(u16)(gPlayerEntity.base.y.HALF.HI - gRoomControls.origin_y);
                            u32 pos_faced = (px >> 4) | (((py - 0x10) >> 4) << 6);
                            u32 tt = GetTileTypeAtTilePos(pos_faced, gPlayerEntity.base.collisionLayer);
                            fprintf(stderr,
                                    "[cof-probe] frame %u: link=(0x%x,0x%x) layer=%u faced=0x%x tt=0x%x act=0x%x\n",
                                    frame, gPlayerEntity.base.x.HALF.HI, gPlayerEntity.base.y.HALF.HI,
                                    gPlayerEntity.base.collisionLayer, pos_faced, tt,
                                    (tt < 0x4000) ? GetActTileForTileType(tt) : tt);
                        }
                        Port_Config_TestForceEdge(8 /* PORT_INPUT_R */);
                        if ((int)frame >= stage_frame + 8) {
                            stage = 4;
                            stage_frame = (int)frame + 90;
                        }
                    }
                    break;
                case 4: /* observe result */
                    if ((int)frame >= stage_frame) {
                        u32 tt_top = GetTileTypeAtTilePos(seatPos, 2);
                        fprintf(stderr,
                                "[cof-probe] frame %u: post-R seat tile top=0x%x flag18=%u smallkeys(dungeon)=%u\n",
                                frame, tt_top, CheckLocalFlag(0x18), (unsigned)gSave.dungeonKeys[1]);
                        stage = 5;
                    }
                    break;
                default:
                    break;
            }
            if (stage < 5)
                return; /* hold off the normal capture until the probe finishes */
        }
    }

    /* ---- TMC_ROOMCAP_LEVER_PROBE: issue #159 root cause -----------------
     * RollobiteSwitch (0x50/0x11): seat the cog (flag 0x2f) so re-entry
     * spawns HITTABLE_LEVER via ObjectOnPillar case 2, then dump the
     * lever's memory at PC offsets 0xAC/0xAE/0xB0/0xB2 (where hitFlag may
     * land depending on struct padding) and simulate a sword hit
     * (contactFlags |= CONTACT_NOW). Watch flags 0x2e (lever) / 0x3d
     * (secret->bollard) to see whether the gate chain fires. */
    {
        static int lprobe = -1;
        static int lstage = 0, lstage_frame = 0;
        if (lprobe < 0)
            lprobe = (getenv("TMC_ROOMCAP_LEVER_PROBE") != NULL) ? 1 : 0;
        if (lprobe && warp_done) {
            extern void SetLocalFlag(u32 flag);
            extern u32 CheckLocalFlag(u32 flag);
            extern Entity* DeepFindEntityByID(u32 kind, u32 id);
            switch (lstage) {
                case 0: /* seat the cog, then reload the room */
                    if ((int)frame >= cap_frame - settle + 120) {
                        SetLocalFlag(0x2f);
                        fprintf(stderr, "[lever-probe] frame %u: set cog-seated flag 0x2f, re-warping\n", frame);
                        lstage = 1;
                    }
                    break;
                case 1:
                    if (frame % 20 == 0) {
                        int rc = Port_DebugAction_Warp((unsigned char)a, (unsigned char)r, (unsigned short)x,
                                                       (unsigned short)y, (unsigned char)l);
                        if (rc == 1) {
                            lstage = 2;
                            lstage_frame = (int)frame + 180;
                        }
                    }
                    break;
                case 2: /* find the spawned lever, dump candidate offsets */
                    if ((int)frame >= lstage_frame) {
                        Entity* lever = DeepFindEntityByID(6 /* OBJECT */, 0x2f /* HITTABLE_LEVER */);
                        if (lever == NULL) {
                            fprintf(stderr, "[lever-probe] frame %u: NO lever entity found\n", frame);
                            lstage = 5;
                            break;
                        }
                        unsigned char* p = (unsigned char*)lever;
                        fprintf(stderr,
                                "[lever-probe] frame %u: lever=%p type=%u pos=(0x%x,0x%x) "
                                "mem[0xAC]=0x%04x mem[0xAE]=0x%04x mem[0xB0]=0x%04x mem[0xB2]=0x%04x "
                                "flag2e=%u flag3d=%u\n",
                                frame, (void*)lever, lever->type, lever->x.HALF.HI, lever->y.HALF.HI,
                                *(unsigned short*)(p + 0xAC), *(unsigned short*)(p + 0xAE),
                                *(unsigned short*)(p + 0xB0), *(unsigned short*)(p + 0xB2), CheckLocalFlag(0x2e),
                                CheckLocalFlag(0x3d));
                        /* simulate a sword hit */
                        lever->contactFlags = 0x80 /* CONTACT_NOW */;
                        lever->iframes = 0;
                        lstage = 3;
                        lstage_frame = (int)frame + 60;
                    }
                    break;
                case 3: /* observe flag chain */
                    if ((int)frame >= lstage_frame) {
                        fprintf(stderr, "[lever-probe] frame %u: post-hit flag2e=%u flag3d=%u\n", frame,
                                CheckLocalFlag(0x2e), CheckLocalFlag(0x3d));
                        lstage = 5;
                    }
                    break;
                default:
                    break;
            }
            if (lstage < 5)
                return;
        }
    }

    /* ---- TMC_ROOMCAP_ROLL_PROBE=<up|right|down|left>: edge-scroll ------
     * Walks Link across the room edge and checks the widescreen rolling
     * transition: the presented width never changes mid-roll, the camera
     * moves monotonically, and it hands over to the follow camera without
     * a jump. Captures <OUT base>_roll_{05,30,end}.png. Exits 0/5. */
    {
        static int rprobe = -2, rstage = 0, rcand = 0, rdir = 1, rwidth = -1, rlast = 0, rstep = 0, rfail = 0;
        static unsigned int rdeadline = 0, rendframe = 0;
        if (rprobe == -2) {
            static const char* const kDirs[] = { "up", "right", "down", "left" };
            const char* e = getenv("TMC_ROOMCAP_ROLL_PROBE");
            rprobe = 0;
            if (e && *e) {
                rprobe = 1;
                for (int i = 0; i < 4; ++i)
                    if (strcmp(e, kDirs[i]) == 0)
                        rdir = i;
            }
        }
        if (rprobe && warp_done && (int)frame >= cap_frame - settle + 120) {
            extern int Port_Widescreen_EffectiveViewWidth(void);
            extern int Port_Widescreen_LatchedFrameWidth(void);
            extern int Port_Widescreen_CameraRestX(int target_x);
            static const unsigned short kKeys[] = { DPAD_UP, DPAD_RIGHT, DPAD_DOWN, DPAD_LEFT };
            const char* base = getenv("TMC_ROOMCAP_OUT");
            char path[512];
            /* Horizontal rolls track the view centre: a roll between rooms of
             * different widths changes the view width, which moves the left
             * edge while the picture's centre holds still. */
            int pos = (rdir & 1) ? gRoomControls.scroll_x + Port_Widescreen_EffectiveViewWidth() / 2
                                 : gRoomControls.scroll_y;
            int latched = Port_Widescreen_LatchedFrameWidth();
            if (!base || !*base)
                base = "roomcap";
            switch (rstage) {
                case 0: /* step to just inside the chosen edge, then walk out */
                    {
                        extern void Port_Config_SetWidescreenEnabled(bool enabled);
                        Port_Config_SetWidescreenEnabled(true);
                    }
                    {
                        /* Try points along the edge until one is an open exit. */
                        int along = 24 + 16 * rcand++;
                        int span = (rdir & 1) ? gRoomControls.height : gRoomControls.width;
                        if (along > span - 24) {
                            fprintf(stderr, "[roll-probe] FAIL no open exit on edge %d\n", rdir);
                            fflush(stderr);
                            _Exit(5);
                        }
                        if (rdir & 1) {
                            gPlayerEntity.base.x.HALF.HI = (s16)(gRoomControls.origin_x +
                                                                  (rdir == 1 ? gRoomControls.width - 6 : 6));
                            gPlayerEntity.base.y.HALF.HI = (s16)(gRoomControls.origin_y + along);
                        } else {
                            gPlayerEntity.base.x.HALF.HI = (s16)(gRoomControls.origin_x + along);
                            gPlayerEntity.base.y.HALF.HI = (s16)(gRoomControls.origin_y +
                                                                  (rdir == 2 ? gRoomControls.height - 6 : 6));
                        }
                    }
                    if (rcand == 1)
                        fprintf(stderr, "[roll-probe] frame %u: room %d/%d %dx%d view=%d, walking %d\n", frame,
                                (int)gRoomControls.area, (int)gRoomControls.room, (int)gRoomControls.width,
                                (int)gRoomControls.height, Port_Widescreen_EffectiveViewWidth(), rdir);
                    rdeadline = frame + 40;
                    rstage = 1;
                    break;
                case 1: /* hold the direction until the roll starts */
                    *(volatile unsigned short*)(gIoMem + KEYINPUT_REG) &= ~kKeys[rdir];
                    if (gRoomControls.scrollAction == 2) {
                        rlast = pos;
                        rstage = 2;
                        fprintf(stderr, "[roll-probe] frame %u: roll start scroll=(%d,%d) view=%d latched=%d\n", frame,
                                (int)gRoomControls.scroll_x, (int)gRoomControls.scroll_y,
                                Port_Widescreen_EffectiveViewWidth(), latched);
                    } else if (frame >= rdeadline) {
                        rstage = 0; /* blocked here: next point */
                    }
                    break;
                case 2: /* rolling: width constant, camera monotonic */
                    if (gRoomControls.scrollAction != 2) {
                        rendframe = frame;
                        rstage = 3;
                        break;
                    }
                    if (gRoomControls.unk_18 >= 1) {
                        if (rwidth < 0)
                            rwidth = latched;
                        if (latched != rwidth) {
                            fprintf(stderr, "[roll-probe] FAIL presented width %d -> %d at step %d\n", rwidth, latched,
                                    (int)gRoomControls.unk_18);
                            rfail = 1;
                        }
                    }
                    if ((rdir == 1 || rdir == 2) ? pos < rlast : pos > rlast) {
                        fprintf(stderr, "[roll-probe] FAIL camera reversed %d -> %d\n", rlast, pos);
                        rfail = 1;
                    }
                    if (gRoomControls.unk_18 >= 2 && (pos - rlast > rstep || rlast - pos > rstep))
                        rstep = pos > rlast ? pos - rlast : rlast - pos;
                    if (getenv("TMC_ROOMCAP_ROLL_TRACE"))
                        fprintf(stderr, "[roll-probe]   step %d scroll=(%d,%d) origin=(%d,%d) player=(%d,%d)\n",
                                (int)gRoomControls.unk_18, (int)gRoomControls.scroll_x, (int)gRoomControls.scroll_y,
                                (int)gRoomControls.origin_x, (int)gRoomControls.origin_y,
                                (int)gPlayerEntity.base.x.HALF.HI, (int)gPlayerEntity.base.y.HALF.HI);
                    rlast = pos;
                    if (gRoomControls.unk_18 == 5 || gRoomControls.unk_18 == 30) {
                        snprintf(path, sizeof(path), "%s_roll_%02d.png", base, (int)gRoomControls.unk_18);
                        RoomCap_Capture(path);
                    }
                    break;
                case 3: { /* first follow-camera frame: no snap */
                    int rest = (rdir & 1) ? Port_Widescreen_CameraRestX(gPlayerEntity.base.x.HALF.HI) : pos;
                    int jump = pos - rlast;
                    fprintf(stderr,
                            "[roll-probe] frame %u: roll end scroll=(%d,%d) rest_x=%d jump=%d step=%d view=%d latched=%d "
                            "rollwidth=%d\n",
                            frame, (int)gRoomControls.scroll_x, (int)gRoomControls.scroll_y, rest, jump, rstep,
                            Port_Widescreen_EffectiveViewWidth(), latched, rwidth);
                    fprintf(stderr,
                            "[roll-probe]   room %d origin=(%d,%d) size=%dx%d player=(%d,%d) target_is_player=%d\n",
                            (int)gRoomControls.room, (int)gRoomControls.origin_x, (int)gRoomControls.origin_y,
                            (int)gRoomControls.width, (int)gRoomControls.height, (int)gPlayerEntity.base.x.HALF.HI,
                            (int)gPlayerEntity.base.y.HALF.HI,
                            gRoomControls.camera_target == &gPlayerEntity.base);
                    if (jump > rstep || jump < -rstep) {
                        fprintf(stderr, "[roll-probe] FAIL camera jumped %dpx at handover (roll step %d)\n", jump,
                                rstep);
                        rfail = 1;
                    }
                    if ((rdir & 1) && gRoomControls.camera_target == &gPlayerEntity.base && gRoomControls.scroll_x != rest) {
                        fprintf(stderr, "[roll-probe] FAIL camera %d not at rest %d\n", (int)gRoomControls.scroll_x,
                                rest);
                        rfail = 1;
                    }
                    rstage = 4;
                    break;
                }
                case 4:
                    if (frame >= rendframe + 30) {
                        snprintf(path, sizeof(path), "%s_roll_end.png", base);
                        RoomCap_Capture(path);
                        fprintf(stderr, "[roll-probe] %s\n", rfail ? "FAIL" : "PASS");
                        fflush(stderr);
                        _Exit(rfail ? 5 : 0);
                    }
                    break;
                default:
                    break;
            }
            return; /* probe owns the run; skip capture */
        }
        if (rprobe && warp_done)
            return;
    }

    /* ---- TMC_ROOMCAP_PAN_PROBE: scripted-pan camera wait (widescreen) ----
     * Reproduces the WaitForCameraTouchRoomBorder deadlock headlessly: the
     * intro festival pan / Zelda-talk scripts CameraTargetEntity onto an
     * orchestrator, Scroll1 pans, then the script busy-waits for scroll_x to
     * EQUAL the rest position it recomputes itself. Teleport the player (the
     * camera target) across the room, let Scroll1 pan, and evaluate the
     * script's own wait each frame via its command handler. PASS = the wait
     * sets flags bit0 within the window; FAIL = deadlock (the reported
     * freeze). Exits 0/5. */
    {
        static int pprobe = -1;
        static int pstage = 0;
        static unsigned int pdeadline = 0;
        static Entity panTarget; /* physics-free coordinate holder, like the
                                  * CUTSCENE_ORCHESTRATOR the intro scripts
                                  * CameraTargetEntity onto */
        if (pprobe < 0)
            pprobe = (getenv("TMC_ROOMCAP_PAN_PROBE") != NULL) ? 1 : 0;
        if (pprobe && warp_done) {
            extern void WaitForCameraTouchRoomBorder(Entity * entity, ScriptExecutionContext * context);
            extern int Port_Widescreen_EffectiveViewWidth(void);
            switch (pstage) {
                case 0: /* park the pan target at an interior rest position */
                    if ((int)frame >= cap_frame - settle + 120) {
                        /* The deadlock under test only exists with the
                         * widescreen option ON (persisted config may have it
                         * off, e.g. toggled off by a user escaping the very
                         * freeze this reproduces). Force it for the probe. */
                        extern void Port_Config_SetWidescreenEnabled(bool enabled);
                        Port_Config_SetWidescreenEnabled(true);
                    }
                    if ((int)frame >= cap_frame - settle + 120) {
                        /* Interior rest: in a 480px room at viewW=384 the
                         * camera rests at target-192 (in (0, 96)) while the
                         * pre-fix wait recomputed target-120 — guaranteed
                         * mismatch, so this probe FAILS on the old code. An
                         * edge target would NOT discriminate (both formulas
                         * clamp to the same bound there). */
                        memset(&panTarget, 0, sizeof(panTarget));
                        panTarget.x.HALF.HI = (s16)(gRoomControls.origin_x + 240);
                        panTarget.y.HALF.HI = (s16)(gRoomControls.origin_y + 0x138);
                        gRoomControls.camera_target = &panTarget;
                        gRoomControls.scrollSpeed = 4;
                        pdeadline = frame + 600; /* 480px room / 4px per frame << 600 */
                        fprintf(stderr, "[pan-probe] frame %u: target -> x=%d (scroll_x=%d viewW=%d), panning\n", frame,
                                (int)panTarget.x.HALF.HI, (int)gRoomControls.scroll_x,
                                Port_Widescreen_EffectiveViewWidth());
                        pstage = 1;
                    }
                    break;
                case 1: { /* evaluate the script wait every frame */
                    ScriptExecutionContext ctx;
                    memset(&ctx, 0, sizeof(ctx));
                    gActiveScriptInfo.flags &= ~1u;
                    WaitForCameraTouchRoomBorder(NULL, &ctx);
                    if ((frame % 20) == 0) {
                        extern int Port_Widescreen_FallbackNative(void);
                        extern int Port_Widescreen_TargetViewWidth(void);
                        extern bool Port_Config_WidescreenEnabled(void);
                        fprintf(stderr,
                                "[pan-probe] frame %u: scroll_x=%d target_x=%d viewW=%d task=%u cfg=%d fallback=%d "
                                "targetW=%d roomW=%d\n",
                                frame, (int)gRoomControls.scroll_x, (int)panTarget.x.HALF.HI,
                                Port_Widescreen_EffectiveViewWidth(), (unsigned)gMain.task,
                                (int)Port_Config_WidescreenEnabled(), Port_Widescreen_FallbackNative(),
                                Port_Widescreen_TargetViewWidth(), (int)gRoomControls.width);
                    }
                    if (gActiveScriptInfo.flags & 1) {
                        fprintf(stderr, "[pan-probe] frame %u: PASS wait satisfied (scroll_x=%d viewW=%d)\n", frame,
                                (int)gRoomControls.scroll_x, Port_Widescreen_EffectiveViewWidth());
                        fflush(stderr);
                        _Exit(0);
                    }
                    if (frame >= pdeadline) {
                        fprintf(stderr, "[pan-probe] frame %u: FAIL deadlock (scroll_x=%d target_x=%d viewW=%d)\n",
                                frame, (int)gRoomControls.scroll_x, (int)panTarget.x.HALF.HI,
                                Port_Widescreen_EffectiveViewWidth());
                        fflush(stderr);
                        _Exit(5);
                    }
                    break;
                }
                default:
                    break;
            }
            return; /* probe owns the run; skip capture */
        }
    }

    /* ---- TMC_ROOMCAP_TOWN_PROBE: rando festival-softlock repro ---------
     * Story-skip leaves global_progress==1 (until Deepwood) with TABIDACHI
     * set. sub_unk3_HyruleTown_0 must NOT redirect the real Hyrule Town
     * (area 0x02) to the festival town (0x15) once TABIDACHI is set, or the
     * town loads in Picori-festival state while its exits are post-festival
     * -> softlock. Warp to town: TMC_ROOMCAP_WARP=0x02,0,<x>,<y>.
     *   =1  set TABIDACHI  -> PASS if the town stays area 0x02 (the fix)
     *   =2  leave it clear -> PASS if it still redirects to 0x15 (intro intact)
     * Exits 0 (pass) / 5 (fail). */
    {
        static int tprobe = -1, tstage = 0;
        extern void SetGlobalFlag(u32 flag);
        extern u32 CheckGlobalFlag(u32 flag);
        if (tprobe < 0) {
            const char* e = getenv("TMC_ROOMCAP_TOWN_PROBE");
            tprobe = (e && *e) ? atoi(e) : 0;
        }
        if (tprobe && warp_done) {
            switch (tstage) {
                case 0: {
                    extern void ClearGlobalFlag(u32 flag);
                    if (tprobe == 1)
                        SetGlobalFlag(0x15); /* TABIDACHI = intro over */
                    /* Clear the residue of the bootstrap warp's redirect so
                     * ZELDA_CHASE becomes a clean signal: the fix leaves it
                     * clear (redirect not taken); the control re-sets it. */
                    ClearGlobalFlag(0x1c); /* ZELDA_CHASE */
                    tstage = 1;
                    break;
                }
                case 1: {
                    /* Re-warp so sub_unk3_HyruleTown_0 re-evaluates with the
                     * flag now in its intended (post-intro) state. */
                    int rc = Port_DebugAction_Warp(2, 0, (unsigned short)x, (unsigned short)y, (unsigned char)l);
                    if (rc == 1) {
                        tstage = 2;
                        cap_frame = (int)(frame + 90);
                    }
                    break;
                }
                case 2:
                    if ((int)frame >= cap_frame) {
                        unsigned area = (unsigned)gRoomControls.area;
                        int chase = (int)CheckGlobalFlag(0x1c); /* ZELDA_CHASE */
                        unsigned want = (tprobe == 1) ? 0x02u : 0x15u;
                        fprintf(stderr, "[town-probe] mode=%d area=0x%02x zelda_chase=%d (want area=0x%02x)\n", tprobe,
                                area, chase, want);
                        fflush(stderr);
                        _Exit(area == want ? 0 : 5);
                    }
                    break;
                default:
                    break;
            }
            return; /* probe owns the run */
        }
    }

    /* Open the existing item screen after the room settles, without changing saves. */
    {
        static int menu_opened = 0;
        if (getenv("TMC_ROOMCAP_MENU") && !menu_opened && warp_done && cap_frame &&
            (int)frame >= cap_frame - settle / 2) {
            extern void InitPauseMenu(void);
            gPauseMenuOptions.screen = PauseMenuScreen_1;
            InitPauseMenu();
            menu_opened = 1;
            fprintf(stderr, "[roomcap] opened inventory menu\n");
        }
    }

    /* TMC_ROOMCAP_MSG=<index>[,posY]: open a textbox <settle/2> frames after
     * the warp so the capture shows live dialogue (widescreen centering
     * test). Optional posY overrides the box row (1 = top-anchored). */
    {
        static int msg_done = 0;
        static unsigned int msg_opened_at = 0;
        const char* m = getenv("TMC_ROOMCAP_MSG");
        if (m && *m && !msg_done && warp_done && cap_frame && (int)frame >= cap_frame - settle / 2) {
            unsigned int idx = 0;
            int posY = -1;
            sscanf(m, "%i,%i", &idx, &posY);
            MessageRequest(idx);
            if (posY >= 0) {
                gMessage.textWindowPosY = (u8)posY;
                msg_posY = posY;
            }
            fprintf(stderr, "[roomcap] frame %u: opened message 0x%x posY=%d\n", frame, idx, posY);
            msg_done = 1;
            msg_opened_at = frame;
        }
        /* Text streams can carry their own position token during initialization. */
        if (msg_done && msg_posY >= 0)
            gTextRender.message.textWindowPosY = (u8)msg_posY;
        /* TMC_ROOMCAP_MSG_ADVANCE=1: tap A every ~40 frames after the box
         * opens so multi-page text rolls forward — but stop on the enquiry
         * (choice) page so the capture shows the live selector instead of
         * confirming through it. renderStatus 5 == RENDER_ENQUIRY. */
        const char* adv = getenv("TMC_ROOMCAP_MSG_ADVANCE");
        if (msg_done && adv && *adv && strcmp(adv, "0") != 0 && gTextRender.renderStatus != 5 &&
            frame > msg_opened_at + 30 && (frame - msg_opened_at) % 40 < 2) {
            extern void Port_Config_TestForceEdge(int input);
            Port_Config_TestForceEdge(0 /* PORT_INPUT_A */);
        }
    }

    /* TMC_ROOMCAP_CLEAN: the room alone, to build a room's picture from -- the
     * game's own HUD hide flags (HUD_HIDE_ALL) and Link not drawn, held
     * every frame from the warp to the capture. */
    if (warp_done) {
        static int clean = -1;
        if (clean < 0) {
            const char* c = getenv("TMC_ROOMCAP_CLEAN");
            clean = (c && *c && strcmp(c, "0") != 0) ? 1 : 0;
        }
        if (clean) {
            gHUD.hideFlags = HUD_HIDE_ALL;
            gPlayerEntity.base.spriteSettings.draw = 0;
            /* dialogue or a cutscene that takes control on arrival (the
             * prologue's "Where are you going?"): advance it, as the voxel
             * tour does (port_repro_npc_talk.c) */
            if (gPlayerState.controlMode != CONTROL_ENABLED && frame % 40 < 2) {
                extern void Port_Config_TestForceEdge(int input);
                Port_Config_TestForceEdge(0 /* PORT_INPUT_A */);
            }
        }
    }

    if (warp_done && cap_frame && (int)frame >= cap_frame) {
        /* TMC_ROOMCAP_SAVE: write a quicksave (state_quick.bin) at the warped
         * spot so it can be F6-loaded interactively, then exit. */
        if (getenv("TMC_ROOMCAP_SAVE")) {
            extern int Port_QuickSave(void);
            int sv = Port_QuickSave();
            fprintf(stderr, "[roomcap] quicksave -> %d (state_quick.bin) area=0x%02x room=0x%02x\n", sv,
                    (unsigned)gRoomControls.area, (unsigned)gRoomControls.room);
            fflush(stderr);
            _Exit(sv ? 0 : 4);
        }
        const char* out = getenv("TMC_ROOMCAP_OUT");
        if (!out || !*out)
            out = "roomcap.png";
        if (getenv("TMC_ROOMCAP_MSG") && !(gMessage.state & MESSAGE_ACTIVE)) {
            fprintf(stderr, "[roomcap] expected dialogue is not active\n");
            _Exit(2);
        }
        if (msg_posY >= 0) {
            int bx, by, bw, bh;
            if (!Message_GetWindowRect(&bx, &by, &bw, &bh) || by != msg_posY * 8) {
                fprintf(stderr, "[roomcap] expected dialogue anchor is not active\n");
                _Exit(2);
            }
        }
        if (getenv("TMC_ROOMCAP_MENU") &&
            (gMain.substate != GAMEMAIN_SUBTASK || gPauseMenuOptions.screen != PauseMenuScreen_1)) {
            fprintf(stderr, "[roomcap] expected inventory is not active\n");
            _Exit(2);
        }
        int ok = RoomCap_Capture(out);
        if (ok == 0)
            return;
        fprintf(stderr, "[roomcap] frame %u: captured %s -> %d (area=0x%02x room=0x%02x)\n", frame, out, ok,
                (unsigned)gRoomControls.area, (unsigned)gRoomControls.room);
        /* where the screen shows the room: the camera, room pixels (as the
         * voxel tour logs it) -- a tool places the capture in the room by it */
        fprintf(stderr, "[roomcap] view room=0x%02x/0x%02x scroll=%d,%d size=%d,%d\n",
                (unsigned)gRoomControls.area, (unsigned)gRoomControls.room,
                (int)(gRoomControls.scroll_x - gRoomControls.origin_x),
                (int)(gRoomControls.scroll_y - gRoomControls.origin_y),
                (int)gRoomControls.width, (int)gRoomControls.height);
        const char* dump = getenv("TMC_ROOMCAP_DUMP");
        if (dump && *dump)
            RoomCap_DumpPpu(dump);
        fflush(stderr);
        _Exit(ok > 0 ? 0 : 2);
    }
}
