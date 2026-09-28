/*
 * port/port_repro_npc_talk.c — headless end-to-end "talk to an NPC" check.
 *
 * Repro for the Android bug report "can't talk to NPCs". Drives the REAL
 * input path (Port_Config_TestForceEdge -> Port_UpdateInput -> KEYINPUT ->
 * UpdatePlayerInput -> gPossibleInteraction -> message system), so a failure
 * here is an engine/input regression, not a touch-layer artifact.
 *
 * Enable with TMC_REPRO_NPC_TALK=1 (headless: TMC_AUTOPLAY=1
 * SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy). On Android, where env vars
 * can't be passed, a marker file "repro_npc_talk" in the app data dir (the
 * CWD) enables it too.
 *
 * Optional: TMC_REPRO_NPC_TALK_WARP="area,room,x,y,layer" (0x-hex ok)
 *           default: Smith's forge (Link's house ground floor), prologue.
 *
 * Sequence:
 *   1. Title -> file select: synthesize save 0 spawning at the target room.
 *   2. If a cutscene owns control (prologue Zelda/Smith scene), mash A
 *      through it until CONTROL_ENABLED.
 *   3. Find the nearest NPC (gEntityLists[7], same defensive walk the
 *      a11y scanner uses), hold the direction toward it every frame.
 *   4. Once adjacent (<= 24px), keep holding INTO it and stamp R.
 *   5. PASS when the message system activates (gMessage.state & 1) or the
 *      player enters the talk state. FAIL on timeout.
 *
 * Exits 0 PASS / 1 FAIL / 3 bootstrap-timeout. Prints [npc-talk] lines.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "save.h"
#include "fileselect.h"
#include "room.h"
#include "flags.h"
#include "player.h"
#include "message.h"
#include "entity.h"
#include "kinstone.h"
#include "subtask.h"
#include "asm.h"
#include "port_repro.h"
#include "port_debug_actions.h"
#include "port_runtime_config.h"

extern void SetActiveSave(u32 idx);
extern int Port_IsValidEntityAddr(const void* p);
#include "port_debug_query.h"
extern int Port_CaptureBaseFramebufferPNG(const char* path);
#include "game.h"
#include "port_voxel.h"
#include "port_gba_mem.h"

/* ponytail: debug knob — TMC_VOXEL_TOUR=1 walks every warpable room (optional
 * TMC_VOXEL_TOUR_START=<index>), holding each ~2s and logging
 * "[voxtour] shot <i>/<n> area=.. room=.." once it has settled, so an
 * external grabber can capture the 3D view of the whole game. Link is kept
 * at full health; rooms whose warp never lands are skipped and logged. */
static void VoxelTourTick(unsigned int frame) {
    static int n = -1, idx = 0;
    static unsigned char areas[2048], rooms[2048];
    static unsigned short ws[2048], hs[2048];
    static unsigned int arrived = 0, fired = 0, started = 0;
    if (n < 0) {
        n = 0;
        for (unsigned int a = 0; a < 0x90; ++a) {
            if (!Port_DebugAction_AreaIsWarpable((unsigned char)a))
                continue;
            const int count = Port_DebugQuery_AreaRoomCount((unsigned char)a);
            for (int r = 0; r < count && n < 2048; ++r) {
                unsigned short w = 0, h = 0;
                if (!Port_DebugQuery_RoomDimensions((unsigned char)a, (unsigned char)r, &w, &h))
                    continue;
                areas[n] = (unsigned char)a, rooms[n] = (unsigned char)r, ws[n] = w, hs[n] = h;
                ++n;
            }
        }
        const char* s = getenv("TMC_VOXEL_TOUR_START");
        idx = s ? atoi(s) : 0;
        fprintf(stderr, "[voxtour] %d rooms, starting at %d\n", n, idx);
        started = frame;
    }
    gSave.stats.health = gSave.stats.maxHealth;
    static int end = -1;
    if (end < 0) {
        const char* c = getenv("TMC_VOXEL_TOUR_COUNT");
        end = c ? idx + atoi(c) : n;
        if (end > n)
            end = n;
    }
    if (idx >= end) {
        fprintf(stderr, "[voxtour] done\n");
        fflush(stderr);
        _Exit(0);
    }
    /* Advance dialogue / cutscenes that grab control on arrival. */
    if (gPlayerState.controlMode != CONTROL_ENABLED && frame % 40 < 2)
        Port_Config_TestForceEdge(PORT_INPUT_A);
    /* Arrived = in the room and running (the Deepwood barrel room runs in
     * GAMEMAIN_BARRELUPDATE instead of GAMEMAIN_UPDATE). */
    const int here = gRoomControls.area == areas[idx] && gRoomControls.room == rooms[idx] &&
                     (gMain.substate == GAMEMAIN_UPDATE || gMain.substate == GAMEMAIN_BARRELUPDATE);
    if (here) {
        if (arrived == 0)
            arrived = frame;
        if (frame - arrived == 100) {
            fprintf(stderr,
                    "[voxtour] shot %d/%d area=0x%02x room=0x%02x dispcnt=%04x bg0=%04x bg1=%04x bg2=%04x bg3=%04x "
                    "bg3ofs=%d,%d scroll=%d,%d bld=%04x/%04x win=%04x/%04x\n",
                    idx, n, areas[idx], rooms[idx], gba_read16(0x04000000u), gba_read16(0x04000008u),
                    gba_read16(0x0400000Au), gba_read16(0x0400000Cu), gba_read16(0x0400000Eu),
                    gba_read16(0x0400001Cu) & 0x1FF, gba_read16(0x0400001Eu) & 0x1FF,
                    gRoomControls.scroll_x - gRoomControls.origin_x, gRoomControls.scroll_y - gRoomControls.origin_y,
                    gba_read16(0x04000050u), gba_read16(0x04000052u), gba_read16(0x04000048u),
                    gba_read16(0x0400004Au));
            const char* dir = getenv("TMC_VOXEL_TOUR_DIR");
            if (dir && *dir) {
                if (getenv("TMC_VOXEL_TOUR_DUMP")) {
                    extern u16 gMapDataBottomSpecial[0x4000];
                    extern u16 gMapDataTopSpecial[0x4000];
                    char dp[512];
                    snprintf(dp, sizeof(dp), "%s/%04d.bin", dir, idx);
                    FILE* f = fopen(dp, "wb");
                    if (f) {
                        fwrite(gMapDataBottomSpecial, 1, 0x8000, f);
                        fwrite(gMapDataTopSpecial, 1, 0x8000, f);
                        fwrite(gVram, 1, 0x18000, f);
                        fwrite(gBgPltt, 1, 0x200, f);
                        fwrite(gIoMem, 1, 0x400, f);
                        const short sc[4] = { (short)(gRoomControls.scroll_x - gRoomControls.origin_x),
                                              (short)(gRoomControls.scroll_y - gRoomControls.origin_y),
                                              (short)gRoomControls.width, (short)gRoomControls.height };
                        fwrite(sc, 1, sizeof(sc), f);
                        fclose(f);
                    }
                }
                char path[512];
                snprintf(path, sizeof(path), "%s/%04d_%02x_%02x.png", dir, idx, areas[idx], rooms[idx]);
                Port_Voxel_RequestShot(path);
                snprintf(path, sizeof(path), "%s/%04d_%02x_%02x_2d.png", dir, idx, areas[idx], rooms[idx]);
                Port_CaptureBaseFramebufferPNG(path);
            }
        }
        if (frame - arrived >= 140) {
            ++idx, arrived = 0, fired = 0, started = frame;
        }
        return;
    }
    arrived = 0;
    if (frame - started > 900) {
        fprintf(stderr, "[voxtour] skip %d area=0x%02x room=0x%02x (never arrived)\n", idx, areas[idx], rooms[idx]);
        ++idx, fired = 0, started = frame;
        return;
    }
    if ((fired == 0 || frame - fired > 240) && frame % 10 == 0) {
        unsigned short x = ws[idx] ? ws[idx] / 2 : 0x80, y = hs[idx] ? hs[idx] / 2 : 0x80;
        unsigned char l = 1;
        Port_DebugAction_WarpSpawnOverride(areas[idx], rooms[idx], &x, &y, &l);
        if (Port_DebugAction_Warp(areas[idx], rooms[idx], x, y, l) == 1)
            fired = frame;
    }
}

/* Message globals (engine): gMessage.state bit 0 = message active. */
extern Message gMessage;

static Entity* NearestNpc(int px, int py) {
    LinkedList* list = &gEntityLists[7]; /* NPC list */
    Entity* e;
    Entity* best = NULL;
    int bestD2 = 0x7fffffff;
    int steps = 0;
    for (e = list->first; e != NULL && (intptr_t)e != (intptr_t)list && steps < 256 && Port_IsValidEntityAddr(e);
         e = e->next, ++steps) {
        int dx, dy, d2;
        if (e->flags & ENT_DELETED)
            continue;
        dx = (int)e->x.HALF.HI - px;
        dy = (int)e->y.HALF.HI - py;
        d2 = dx * dx + dy * dy;
        if (d2 < bestD2) {
            bestD2 = d2;
            best = e;
        }
    }
    return best;
}

void Port_ReproNpcTalk_Tick(unsigned int frame) {
    static int active = -1;
    static int booted = 0, warped = 0;
    static int adjacent_since = 0, r_stamped_at = 0;
    static int last_log = 0;
    static int mash_a = 0;
    static int hold_dir = -1;
    static int bootstrap_only = 0;
    static unsigned int a = 0x22, r = 0x11, x = 0x78, y = 0x88, l = 0;
    static unsigned char ea, er; /* where the game is entered for (a, r); see Port_DebugAction_WarpEntry */
    static unsigned short ex;

    if (active < 0) {
        const char* env = getenv("TMC_REPRO_NPC_TALK");
        const char* roundtrip = getenv("TMC_REPRO_QUICKSAVE_ROUNDTRIP");
        bootstrap_only = roundtrip && *roundtrip && strcmp(roundtrip, "0") != 0;
        active = (env && *env && strcmp(env, "0") != 0) || bootstrap_only;
        if (!active) {
            FILE* marker = fopen("repro_npc_talk", "rb");
            if (marker) {
                fclose(marker);
                active = 1;
            }
        }
        if (active) {
            const char* w = getenv("TMC_REPRO_NPC_TALK_WARP");
            if (w && *w)
                sscanf(w, "%i,%i,%i,%i,%i", &a, &r, &x, &y, &l);
            ea = (unsigned char)a, er = (unsigned char)r, ex = (unsigned short)x;
            Port_DebugAction_WarpEntry(&ea, &er, &ex);
            mash_a = getenv("TMC_REPRO_MASH_A") != NULL; /* ponytail: debug knob — mash A every 40f */
            {
                /* ponytail: debug knob — hold a direction every frame (walk through doors) */
                const char* hd = getenv("TMC_REPRO_HOLD_DIR");
                if (hd && *hd) {
                    hold_dir = (*hd == 'u')   ? PORT_INPUT_UP
                               : (*hd == 'd') ? PORT_INPUT_DOWN
                               : (*hd == 'l') ? PORT_INPUT_LEFT
                                              : PORT_INPUT_RIGHT;
                }
            }
            fprintf(stderr, "[npc-talk] active warp=0x%02x/0x%02x (%u,%u) layer=%u\n", a, r, x, y, l);
        }
    }
    if (!active)
        return;

    if (mash_a && warped && frame % 40 < 2) {
        Port_Config_TestForceEdge(PORT_INPUT_A);
        if (getenv("TMC_REPRO_MASH_B")) /* ponytail: debug knob — sword mash for boss-fight repros */
            Port_Config_TestForceEdge(PORT_INPUT_B);
    }
    if (hold_dir >= 0 && gMain.task == TASK_GAME)
        Port_Config_TestForceEdge(hold_dir);

    if (!booted && gMain.task == TASK_TITLE && frame >= 30 && (frame & 0xF) < 3) {
        Port_Config_TestForceEdge(PORT_INPUT_START);
    }

    if (!booted && gMain.task == TASK_FILE_SELECT && frame > 60) {
        SaveFile* sv = &gFileSelectState.saves[0];
        int slot = 0;
        /* ponytail: debug knob — keep the on-disk saves (real player data,
           first initialized slot) instead of synthesizing; warp still applies. */
        if (getenv("TMC_REPRO_KEEP_SAVE")) {
            while (slot < 2 && !gFileSelectState.saves[slot].initialized) {
                slot++;
            }
            fprintf(stderr, "[npc-talk] keep-save: using slot %d (init=%d)\n", slot,
                    (int)gFileSelectState.saves[slot].initialized);
        } else {
            ResetSaveFile(0);
            sv->initialized = 1;
            sv->name[0] = 'A';
            sv->saved_status.area_next = ea;
            sv->saved_status.room_next = er;
            sv->saved_status.start_pos_x = (s16)ex;
            sv->saved_status.start_pos_y = (s16)y;
            sv->saved_status.layer = (u8)l;
            gFileSelectState.saveStatus[0] = 1;
        }
        SetActiveSave(slot);
        SetTask(TASK_GAME);
        /* ponytail: debug knob — post-intro world state for repros of
           fusion-gated world events (opened waterfalls, caves, lilypads).
           =1 intro over + all items + every fusion done; =2 same but with
           fusions left undone, for before/after comparisons. */
        {
            const char* unlock = getenv("TMC_REPRO_WORLD_UNLOCK");
            if (unlock && *unlock) {
                SetGlobalFlag(TABIDACHI);
                SetGlobalFlag(START); /* intro seen: Link's bedroom must not replay it */
                Port_DebugAction_GiveAllItems();
                if (*unlock != '2')
                    Port_DebugAction_AllKinstones();
            }
        }
        booted = 1;
        fprintf(stderr, "[npc-talk] frame %u: bootstrapped -> TASK_GAME\n", frame);
    }

    if (bootstrap_only && booted) {
        return;
    }

    if (booted && !warped && gMain.task == TASK_GAME && frame % 20 == 0) {
        static unsigned int fired_at = 0;
        if (((unsigned)gRoomControls.area == a && (unsigned)gRoomControls.room == r) ||
            (gRoomControls.area == ea && gRoomControls.room == er)) {
            warped = 1;
            fprintf(stderr, "[npc-talk] frame %u: in target room (area=0x%02x room=0x%02x)\n", frame,
                    (unsigned)gRoomControls.area, (unsigned)gRoomControls.room);
        } else if ((fired_at == 0 || frame - fired_at > 180) &&
                   Port_DebugAction_Warp((unsigned char)a, (unsigned char)r, (unsigned short)x, (unsigned short)y,
                                         (unsigned char)l) == 1) {
            /* Re-firing mid-transition restarts the fade forever; give the
             * room swap 3s to land before retrying. */
            fired_at = frame;
            fprintf(stderr, "[npc-talk] frame %u: warp fired (now area=0x%02x room=0x%02x)\n", frame,
                    (unsigned)gRoomControls.area, (unsigned)gRoomControls.room);
        }
    }
    if (!warped)
        return;
    if (getenv("TMC_VOXEL_TOUR")) {
        VoxelTourTick(frame);
        return;
    }

    /* Success oracle: message box opened (talk succeeded). MESSAGE_ACTIVE
     * is 0x7f — any live message phase counts. Check FIRST so the
     * mash/talk logic below can't race it. */
    if ((gMessage.state & 0x7f) != 0 && r_stamped_at != 0) {
        fprintf(stderr, "[npc-talk] frame %u: PASS — message active after R (stamped at %d)\n", frame, r_stamped_at);
        fflush(stderr);
        _Exit(0);
    }

    /* ponytail: debug knob — replay the LIVE kinstone fusion for a
     * fusion-gated world event: TMC_REPRO_FUSE=<kinstoneId> runs exactly what
     * kinstoneMenu.c does on a successful fuse (set the save bit, then
     * MenuFadeIn into the world-event subtask) and reports the entrance tile's
     * act value before and after, so "cutscene played but the door never
     * opened" is observable without playing to the fuser. */
    {
        static int fuse_id = -2;
        static unsigned int fired_at = 0;
        if (fuse_id == -2) {
            const char* e = getenv("TMC_REPRO_FUSE");
            fuse_id = (e && *e) ? (int)strtol(e, NULL, 0) : -1;
        }
        if (fuse_id >= 0) {
            const WorldEvent* ev = &GetWorldEvents()[gKinstoneWorldEvents[fuse_id].worldEventId];
            u32 tilePos = (ev->x >> 4 & 0x3f) | ((ev->y >> 4 & 0x3f) << 6);
            /* Wait for the room to finish initializing: GameMain_ChangeRoom
             * clears the init priority only when it reaches GAMEMAIN_UPDATE.
             * Fusing before that freezes every cutscene entity. */
            if (fired_at == 0 && frame % 20 == 0 && gPriorityHandler.event_priority == PRIO_MIN &&
                gPlayerState.controlMode == CONTROL_ENABLED) {
                fprintf(stderr, "[fuse] frame %u: pre-fuse act=0x%02x at tile %u — fusing kinstone %d (event %u)\n",
                        frame, (unsigned)GetActTileAtTilePos((u16)tilePos, LAYER_BOTTOM), tilePos, fuse_id,
                        (unsigned)gKinstoneWorldEvents[fuse_id].worldEventId);
                gFuseInfo.kinstoneId = (u8)fuse_id;
                WriteBit(gSave.kinstones.fusedKinstones, (u32)fuse_id);
                MenuFadeIn(SUBTASK_WORLDEVENT, gKinstoneWorldEvents[fuse_id].worldEventId);
                fired_at = frame;
            } else if (fired_at != 0 && (frame - fired_at) % 60 == 0) {
                fprintf(stderr, "[fuse] +%u: entrance act=0x%02x (0x28 = open) area=0x%02x\n", frame - fired_at,
                        (unsigned)GetActTileAtTilePos((u16)tilePos, LAYER_BOTTOM), (unsigned)gRoomControls.area);
            }
        }
    }

    /* Cutscene / scripted intro: advance any dialogue with A until the
     * player has control. (Fresh flags in the prologue forge trigger the
     * Zelda+Smith scene.) */
    if (gPlayerState.controlMode != CONTROL_ENABLED) {
        if (frame % 40 < 2) {
            Port_Config_TestForceEdge(PORT_INPUT_A);
        }
        return;
    }

    /* Walk-only mode (TMC_REPRO_HOLD_DIR): just walk the held direction and
     * report every room change — the oracle for "does this doorway work". The
     * NPC hunt below would otherwise steer Link away every frame. */
    if (hold_dir >= 0) {
        static unsigned int last_area = 0xffffu, last_room = 0xffffu;
        /* ponytail: debug knob — walk through collision, to tell "blocked by
         * collision" apart from "trigger never fires" at a doorway. */
        if (getenv("TMC_REPRO_NOCLIP"))
            Port_DebugAction_SetNoclip(1);
        if (gRoomControls.area != last_area || gRoomControls.room != last_room) {
            last_area = gRoomControls.area;
            last_room = gRoomControls.room;
            fprintf(stderr, "[npc-talk] frame %u: room area=0x%02x room=0x%02x player=(%d,%d)\n", frame, last_area,
                    last_room, (int)gPlayerEntity.base.x.HALF.HI, (int)gPlayerEntity.base.y.HALF.HI);
            fflush(stderr);
        }
        if (frame % 60 == 0)
            fprintf(stderr, "[walk] frame %u: local=(%d,%d) action=%u swim=%u ctl=%u\n", frame,
                    (int)(gPlayerEntity.base.x.HALF.HI - gRoomControls.origin_x),
                    (int)(gPlayerEntity.base.y.HALF.HI - gRoomControls.origin_y),
                    (unsigned)gPlayerEntity.base.action, (unsigned)gPlayerState.swim_state,
                    (unsigned)gPlayerState.controlMode);
        return;
    }

    {
        int px = (int)gPlayerEntity.base.x.HALF.HI;
        int py = (int)gPlayerEntity.base.y.HALF.HI;
        Entity* npc = NearestNpc(px, py);
        int dx, dy, adx, ady, adjacent;
        if (npc == NULL) {
            if (frame - last_log > 120) {
                last_log = (int)frame;
                fprintf(stderr, "[npc-talk] frame %u: no NPC in room yet (px=%d py=%d)\n", frame, px, py);
            }
            if (frame > 20000) {
                fprintf(stderr, "[npc-talk] FAIL: no NPC ever appeared\n");
                fflush(stderr);
                _Exit(1);
            }
            return;
        }

        dx = (int)npc->x.HALF.HI - px;
        dy = (int)npc->y.HALF.HI - py;
        adx = dx < 0 ? -dx : dx;
        ady = dy < 0 ? -dy : dy;
        adjacent = (adx <= 14 && ady <= 24) || (adx <= 24 && ady <= 14);

        if (frame - last_log > 120) {
            last_log = (int)frame;
            fprintf(stderr, "[npc-talk] frame %u: npc id=0x%02x d=(%d,%d) player=(%d,%d) anim=%u fs=%u msg=0x%x\n",
                    frame, (unsigned)npc->id, dx, dy, px, py, (unsigned)gPlayerEntity.base.animationState,
                    (unsigned)gPlayerState.framestate, (unsigned)gMessage.state);
        }

        /* Hold the dominant direction toward the NPC — walking into it is
         * exactly how a player talks. Keep holding while stamping R. */
        if (adx > ady) {
            Port_Config_TestForceEdge(dx > 0 ? PORT_INPUT_RIGHT : PORT_INPUT_LEFT);
        } else {
            Port_Config_TestForceEdge(dy > 0 ? PORT_INPUT_DOWN : PORT_INPUT_UP);
        }

        if (adjacent) {
            if (adjacent_since == 0) {
                adjacent_since = (int)frame;
                fprintf(stderr, "[npc-talk] frame %u: adjacent to npc id=0x%02x — stamping R\n", frame,
                        (unsigned)npc->id);
            }
            /* Give facing time to settle, then release R between presses.
             * Edge stamps become held GBA keys; consecutive stamps produce
             * only one newKeys edge, which may occur before we reach the NPC. */
            if ((int)frame > adjacent_since + 2 && (frame & 1u) != 0) {
                Port_Config_TestForceEdge(PORT_INPUT_R);
                if (r_stamped_at == 0)
                    r_stamped_at = (int)frame;
            }
            if (r_stamped_at != 0 && (int)frame > r_stamped_at + 240) {
                fprintf(stderr,
                        "[npc-talk] FAIL: R stamped for 240 frames, message never opened "
                        "(msg=0x%x fs=%u interactType-check never fired)\n",
                        (unsigned)gMessage.state, (unsigned)gPlayerState.framestate);
                fflush(stderr);
                _Exit(1);
            }
        } else {
            adjacent_since = 0;
        }
    }

    if (frame >= 30000) {
        fprintf(stderr, "[npc-talk] timeout: booted=%d warped=%d task=%u ctl=%u\n", booted, warped,
                (unsigned)gMain.task, (unsigned)gPlayerState.controlMode);
        fflush(stderr);
        _Exit(3);
    }
}
