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
#include "main.h"       /* gMain, SetTask, TASK_* */
#include "save.h"       /* SaveFile, gSave */
#include "fileselect.h" /* gMapDataBottomSpecial, ResetSaveFile */
#include "room.h"       /* gRoomControls */
#include "message.h"    /* gMessage, MessageRequest (TMC_ROOMCAP_MSG hook) */
#include "port_repro.h"
#include "port_gba_mem.h" /* gIoMem, gVram, gBgPltt, gObjPltt, gOamMem */
#include "port_debug_actions.h"

extern int Port_CaptureBaseFramebufferPNG(const char* path);
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
            if (gMain.task == TASK_FILE_SELECT && frame > 200 && objon) {
                const char* base = getenv("TMC_ROOMCAP_OUT");
                if (!base || !*base)
                    base = "fsel.png";
                if (fsel_first == 0) {
                    char p[512];
                    snprintf(p, sizeof(p), "%s_a.png", base);
                    int ok = Port_CaptureBaseFramebufferPNG(p);
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
                    int ok = Port_CaptureBaseFramebufferPNG(p);
                    fprintf(stderr, "[roomcap-fsel] frame %u: captured %s -> %d\n", frame, p, ok);
                    const char* dumpb = getenv("TMC_ROOMCAP_DUMP");
                    if (dumpb && *dumpb) {
                        char d[512];
                        snprintf(d, sizeof(d), "%s_b.bin", dumpb);
                        RoomCap_DumpPpu(d);
                    }
                    fflush(stderr);
                    _Exit(ok ? 0 : 2);
                }
            }
            if (frame == 5000) {
                fprintf(stderr, "[roomcap-fsel] timeout (task=%u)\n", (unsigned)gMain.task);
                fflush(stderr);
                _Exit(3);
            }
            return; /* don't fall through to the room-bootstrap path */
        }
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
            if (posY >= 0)
                gMessage.textWindowPosY = (u8)posY;
            fprintf(stderr, "[roomcap] frame %u: opened message 0x%x posY=%d\n", frame, idx, posY);
            msg_done = 1;
            msg_opened_at = frame;
        }
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
        int ok = Port_CaptureBaseFramebufferPNG(out);
        fprintf(stderr, "[roomcap] frame %u: captured %s -> %d (area=0x%02x room=0x%02x)\n", frame, out, ok,
                (unsigned)gRoomControls.area, (unsigned)gRoomControls.room);
        const char* dump = getenv("TMC_ROOMCAP_DUMP");
        if (dump && *dump)
            RoomCap_DumpPpu(dump);
        fflush(stderr);
        _Exit(ok ? 0 : 2);
    }

    if (frame == 5000) {
        fprintf(stderr, "[roomcap] timeout (booted=%d task=%u warp=%d)\n", booted, (unsigned)gMain.task, warp_done);
        fflush(stderr);
        _Exit(3);
    }
}
