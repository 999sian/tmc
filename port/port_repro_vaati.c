/*
 * port/port_repro_vaati.c — Issue #151 Vaati-transform crash reproduction.
 *
 * TMC_REPRO_VAATI=1 drives title -> game, primes the Dark Hyrule Castle
 * final-boss room state (Darknuts defeated, V1 intro not played), warps into
 * area 0x88 room 0x6, lets the normal room-init path load script_Vaati1Intro
 * plus the Vaati Reborn enemy, and pulses A through the intro text so the V1
 * boss entity actually starts. TMC_VAATI_SLOT=<slot> skips the warp and loads
 * an existing quicksave slot instead. TMC_VAATI_DAMAGE_TEST=1 warps straight
 * to fresh Phase 2, forces the real eye reveal/strike sequence, then applies
 * the clone-charge body hit; it exits nonzero if Vaati never becomes vulnerable
 * or his health never drops.
 *
 * Run under gdb for a real DWARF backtrace (the in-process crash handler's
 * frame walk is unreliable on the -O2 release build):
 *   TMC_REPRO_VAATI=1 TMC_AUTOPLAY=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
 *     gdb -q -batch -ex run -ex bt -ex 'bt full' --args ./tmc_pc
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"         /* gMain, TASK_* */
#include "entity.h"       /* Entity, GenericEntity, MAX_ENTITIES, NPC */
#include "npc.h"          /* VAATI, VAATI_REBORN */
#include "enemy.h"        /* VAATI_* enemy ids */
#include "flags.h"        /* SetLocalFlag, ClearLocalFlag, SetRoomFlag, ClearRoomFlag, CheckRoomFlag */
#include "room.h"         /* gRoomControls */
#include "player.h"       /* gPlayerState */
#include "port_gba_mem.h" /* gIoMem */

extern int Port_DebugAction_Warp(unsigned char area, unsigned char room, unsigned short x, unsigned short y,
                                 unsigned char layer);
extern void VaatiTransfigured(Entity* this);
extern GenericEntity gEntities[MAX_ENTITIES];
extern int Port_QuickSave_LoadSlot(int slot);

#define KEYINPUT_REG 0x130
#define A_BUTTON 0x0001
#define START_BUTTON 0x0008


static int EnvInt(const char* name, int fallback) {
    const char* v = getenv(name);
    if (v == NULL || *v == '\0')
        return fallback;
    return (int)strtol(v, NULL, 0);
}

static void PrepareVaatiIntroFlags(void) {
    /* Match the post-Darknut, pre-V1-intro state before room init runs. */
    SetLocalFlag(0x77);
    ClearLocalFlag(0x75);
    ClearLocalFlag(0x76);
    ClearLocalFlag(0x78);
    ClearLocalFlag(0x7b);
    ClearRoomFlag(0);
    ClearRoomFlag(1);
    ClearRoomFlag(2);
    ClearRoomFlag(3);
}

void Port_ReproVaati_Tick(unsigned int frame) {
    static int active = -1;
    static int slot = -2;
    static int warp_done = 0;
    static int slot_loaded = 0;
    static int spawned = 0;
    static int forced_start = 0;
    static int fight_started = 0;
    static unsigned int spawn_frame = 0;
    static unsigned int fight_start_frame = 0;
    static int damage_test = -1;
    static int damage_started = 0;
    static int damage_vulnerable = 0;
    static unsigned int damage_start_frame = 0;

    if (active < 0) {
        const char* v = getenv("TMC_REPRO_VAATI");
        active = (v && *v && strcmp(v, "0") != 0) ? 1 : 0;
        if (active) {
            slot = EnvInt("TMC_VAATI_SLOT", -1);
            damage_test = EnvInt("TMC_VAATI_DAMAGE_TEST", 0) != 0;
            if (slot >= 0) {
                fprintf(stderr, "[vaati repro] target area=0x88 room=0x6 (final boss); loading slot %d\n", slot);
            } else {
                fprintf(stderr, "[vaati repro] target area=0x88 room=0x6 (final boss)\n");
            }
        }
    }
    if (!active)
        return;

    {
        unsigned short presses = 0;
        if (!warp_done) {
            if (gMain.task == TASK_TITLE && frame >= 30 && (frame & 0xF) < 3)
                presses |= START_BUTTON;
            if (gMain.task == TASK_FILE_SELECT && (frame & 0xF) < 3)
                presses |= A_BUTTON;
        } else if (spawned && (frame & 0xF) < 3) {
            /* Clear the Vaati intro textbox; otherwise the old repro timed out
             * with only the scripted NPC alive and never exercised the fight. */
            presses |= A_BUTTON;
        }
        if (presses)
            *(volatile unsigned short*)(gIoMem + KEYINPUT_REG) &= ~presses;
    }

    if (slot >= 0 && !slot_loaded && frame > 60) {
        if (!Port_QuickSave_LoadSlot(slot)) {
            fprintf(stderr, "[vaati repro] slot %d load failed\n", slot);
            fflush(stderr);
            _Exit(4);
        }
        fprintf(stderr, "[vaati repro] frame %u: loaded quicksave slot %d\n", frame, slot);
        slot_loaded = 1;
        warp_done = 1;
        spawned = 1;
        spawn_frame = frame;
    }

    if (slot < 0 && gMain.task == TASK_GAME && !warp_done && (frame % 60 == 0) && frame > 600) {
        int rc;
        if (damage_test) {
            ClearLocalFlag(0x7b);
            ClearRoomFlag(0);
            ClearRoomFlag(1);
            ClearRoomFlag(2);
            ClearRoomFlag(3);
            rc = Port_DebugAction_Warp(0x8c, 0x0, 0xb0, 0xc0, 1);
        } else {
            PrepareVaatiIntroFlags();
            rc = Port_DebugAction_Warp(0x88, 0x6, 0x78, 0x60, 1);
        }
        fprintf(stderr, "[vaati repro] frame %u: warp -> %d\n", frame, rc);
        if (rc == 1) {
            warp_done = 1;
            spawned = 1;
            spawn_frame = frame;
        }
    }

    if (spawned && gRoomControls.camera_target != NULL) {
        /* Force the camera to its target so the cutscene's
         * WaitForCameraTouchRoomBorder passes (the headless camera never
         * converges on its own), letting Vaati_Apparate + the transform run. */
        s32 left = gRoomControls.camera_target->x.HALF.HI - 120;
        s32 bottom = gRoomControls.camera_target->y.HALF.HI - 80;
        if (left < gRoomControls.origin_x)
            left = gRoomControls.origin_x;
        if (left > gRoomControls.origin_x + gRoomControls.width - 240)
            left = gRoomControls.origin_x + gRoomControls.width - 240;
        if (bottom < gRoomControls.origin_y)
            bottom = gRoomControls.origin_y;
        if (bottom > gRoomControls.origin_y + gRoomControls.height - 160)
            bottom = gRoomControls.origin_y + gRoomControls.height - 160;
        gRoomControls.scroll_x = left;
        gRoomControls.scroll_y = bottom;
    }

    if (spawned) {
        int npcs = 0, enemies = 0, objs = 0, v1 = 0, balls = 0, v2 = 0, wrath = 0, arms = 0, eyes = 0,
            ready_correct_eyes = 0, i;
        Entity* v2_body = NULL;
        const int log_now = ((int)(frame - spawn_frame) % 30) == 0;
        for (i = 0; i < MAX_ENTITIES; i++) {
            Entity* e = &gEntities[i].base;
            if (e->next == NULL)
                continue;
            if (e->kind == NPC) {
                npcs++;
                if (log_now && (e->id == VAATI || e->id == VAATI_REBORN)) {
                    fprintf(stderr, "[vaati repro]    NPC id=0x%02X type=%u action=%u sub=%u timer=%u\n", e->id,
                            e->type, e->action, e->subAction, e->timer);
                }
            } else if (e->kind == ENEMY) {
                enemies++;
                if (e->id == VAATI_REBORN_ENEMY) {
                    v1++;
                } else if (e->id == VAATI_BALL) {
                    balls++;
                } else if (e->id == VAATI_TRANSFIGURED) {
                    v2++;
                    if (e->type == 0 && (v2_body == NULL || e->action != 0))
                        v2_body = e;
                } else if (e->id == VAATI_TRANSFIGURED_EYE) {
                    eyes++;
                    if (e->type != 0 && e->timer != 0 && e->action == 2)
                        ready_correct_eyes++;
                } else if (e->id == VAATI_WRATH) {
                    wrath++;
                } else if (e->id == VAATI_ARM) {
                    arms++;
                }
            } else if (e->kind == OBJECT) {
                objs++;
            }
        }
        if (!forced_start && v1 != 0 && balls == 0 && CheckRoomFlag(0) == 0 && frame > spawn_frame + 360) {
            /* Headless runs can strand script_Vaati1Intro before its SetRoomFlag
             * 0 beat even with the camera pinned. Trigger the same room flag so
             * the already-loaded GBA boss entity enters its real V1 state. */
            SetRoomFlag(0);
            forced_start = 1;
            fprintf(stderr, "[vaati repro] frame %u: forced V1 start room flag\n", frame);
        }
        if (!fight_started && (balls != 0 || v2 != 0 || wrath != 0)) {
            fight_started = 1;
            fight_start_frame = frame;
            fprintf(stderr, "[vaati repro] frame %u: fight started (v1=%d balls=%d v2=%d wrath=%d arms=%d)\n", frame,
                    v1, balls, v2, wrath, arms);
        }
        if (damage_test && v2_body != NULL && v2_body->action == 0 && CheckRoomFlag(0) == 0) {
            SetRoomFlag(0);
            fprintf(stderr, "[vaati repro] frame %u: damage test forced V2 start room flag\n", frame);
        }
        if (damage_test && v2_body != NULL && v2_body->action != 0 && damage_started == 0 &&
            ready_correct_eyes != 0 && frame > fight_start_frame + 30) {
            int poked = 0;
            for (i = 0; i < MAX_ENTITIES; i++) {
                Entity* e = &gEntities[i].base;
                if (e->next != NULL && e->kind == ENEMY && e->id == VAATI_TRANSFIGURED_EYE && e->type != 0 &&
                    e->timer != 0 && e->action == 2) {
                    e->contactFlags = CONTACT_NOW | 0xe;
                    poked++;
                }
            }
            damage_started = 1;
            damage_start_frame = frame;
            fprintf(stderr, "[vaati repro] frame %u: damage test revealed %d correct phase-2 eyes\n", frame, poked);
        }
        if (damage_test && damage_started == 1 && v2_body != NULL && ((const u8*)v2_body)[0xAD] == 1 &&
            ready_correct_eyes != 0) {
            int poked = 0;
            for (i = 0; i < MAX_ENTITIES; i++) {
                Entity* e = &gEntities[i].base;
                if (e->next != NULL && e->kind == ENEMY && e->id == VAATI_TRANSFIGURED_EYE && e->type != 0 &&
                    e->timer != 0 && e->action == 2) {
                    e->health = 0xfe;
                    e->contactFlags = CONTACT_NOW;
                    poked++;
                }
            }
            damage_started = 2;
            fprintf(stderr, "[vaati repro] frame %u: damage test struck %d revealed phase-2 eyes\n", frame, poked);
        }
        if (damage_test && damage_started >= 2 && !damage_vulnerable && v2_body != NULL && v2_body->action == 3) {
            damage_vulnerable = 1;
            gPlayerState.chargeState.action = 5;
            v2_body->contactFlags = CONTACT_NOW | 0xa;
            fprintf(stderr, "[vaati repro] frame %u: damage test Vaati vulnerable, applying clone-charge hit\n", frame);
            VaatiTransfigured(v2_body);
            if (v2_body->health <= 0xc0) {
                fprintf(stderr, "[vaati repro] frame %u: damage test PASS health=0x%02x animationState=%u\n", frame,
                        v2_body->health, v2_body->animationState);
                fflush(stderr);
                _Exit(0);
            }
        } else if (damage_test && damage_vulnerable && v2_body != NULL) {
            gPlayerState.chargeState.action = 5;
            v2_body->contactFlags = CONTACT_NOW | 0xa;
            VaatiTransfigured(v2_body);
            if (v2_body->health <= 0xc0) {
                fprintf(stderr, "[vaati repro] frame %u: damage test PASS health=0x%02x animationState=%u\n", frame,
                        v2_body->health, v2_body->animationState);
                fflush(stderr);
                _Exit(0);
            }
        }
        if (log_now) {
            fprintf(stderr, "[vaati repro] frame %u: npc=%d enemy=%d obj=%d v1=%d balls=%d v2=%d eyes=%d ready=%d "
                            "wrath=%d arms=%d roomFlags=%d/%d/%d/%d plState=%u camTgt=%p",
                    frame, npcs, enemies, objs, v1, balls, v2, eyes, ready_correct_eyes, wrath, arms,
                    CheckRoomFlag(0), CheckRoomFlag(1), CheckRoomFlag(2), CheckRoomFlag(3), gPlayerState.framestate,
                    (void*)gRoomControls.camera_target);
            if (damage_test && v2_body != NULL) {
                const u8* b = (const u8*)v2_body;
                fprintf(stderr, " body(action=%u sub=%u health=0x%02x anim=%u u75=0x%02x u76=0x%02x u81=0x%02x)",
                        v2_body->action, v2_body->subAction, v2_body->health, v2_body->animationState, b[0xA1],
                        b[0xA2], b[0xAD]);
            }
            fputc('\n', stderr);
        }
        if (!fight_started && frame > spawn_frame + 1800) {
            fprintf(stderr, "[vaati repro] FAIL: intro did not reach a Vaati boss entity\n");
            fflush(stderr);
            _Exit(2);
        }
        if (damage_test && damage_started && frame > damage_start_frame + 900) {
            fprintf(stderr, "[vaati repro] FAIL: phase-2 damage test did not damage Vaati (vulnerable=%d health=%d)\n",
                    damage_vulnerable, v2_body != NULL ? v2_body->health : -1);
            fflush(stderr);
            _Exit(5);
        }
        if (!damage_test && fight_started && frame > fight_start_frame + 900) {
            fprintf(stderr, "[vaati repro] no crash after 900 fight frames; done\n");
            fflush(stderr);
            _Exit(0);
        }
    }

    if (frame == 8000) {
        fprintf(stderr, "[vaati repro] timeout (warp=%d spawned=%d fight=%d)\n", warp_done, spawned, fight_started);
        fflush(stderr);
        _Exit(3);
    }
}
