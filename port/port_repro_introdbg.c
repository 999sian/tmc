/*
 * port/port_repro_introdbg.c — Smith's-house intro cutscene diagnostic harness.
 *
 * Drives the game's OWN new-game flow (press START at title, then A on the empty
 * file-select slot to confirm a new game) so the intro cutscene runs naturally
 * via the engine's room load + state-change handler — NOT via a debug warp, which
 * skips the cutscene on both regions. Then logs, per frame:
 *   - task / area / room
 *   - gPlayerState.controlMode (CONTROL_DISABLED=0 means Link is frozen)
 *   - gActiveScriptInfo.syncFlags (the Smith<->Zelda handshake)
 *   - intro NPC visibility (SMITH=0x22, ZELDA=0x28): spawned? draw? animating?
 *
 * Enable: TMC_REPRO_INTRODBG=1 TMC_AUTOPLAY=1 SDL_VIDEODRIVER=dummy
 * Combine with TMC_TRACE_INTRO=1 (script.c control-mode trace) and TMC_VERBOSE=1
 * (sync-flag trace). Compare JP (baserom_jp.gba) vs USA (baserom.gba).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"
#include "save.h"
#include "fileselect.h"
#include "room.h"
#include "entity.h"
#include "player.h"
#include "script.h"
#include "message.h"
#include "port_repro.h"
#include "port_gba_mem.h"

/* GBA KEYINPUT register: bits are ACTIVE-LOW (0 = pressed). */
#define KEYINPUT_REG  0x130
#define KEY_A         0x0001
#define KEY_START     0x0008

static void IntroDbg_PressKey(unsigned short key) {
    *(volatile unsigned short*)(gIoMem + KEYINPUT_REG) &= ~key;
}

/* Probe the two intro NPCs by id and report whether they spawned / are drawn. */
static void IntroDbg_DumpNpcs(void) {
    Entity* head = (Entity*)&gEntityLists[NPC];
    Entity* e;
    int n = 0;
    for (e = gEntityLists[NPC].first; e != NULL && e != head && n < 16; e = e->next, n++) {
        fprintf(stderr,
                "[introdbg]   NPC id=0x%02x type=0x%02x action=%u draw=%u animIdx=%u frameIdx=%u pos=(%d,%d)\n",
                e->id, e->type, e->action,
                (unsigned)e->spriteSettings.draw, (unsigned)e->animIndex,
                (unsigned)e->frameIndex, e->x.HALF.HI, e->y.HALF.HI);
    }
    if (n == 0)
        fprintf(stderr, "[introdbg]   (no NPCs in list)\n");

    /* Reliable by-id probe (no list-walk dependency): SMITH=0x22, ZELDA=0x28.
     * Resolve the sprite's current animation data to distinguish
     * not-spawned vs spawned-draw0 vs drawn-but-unresolved-frame. */
    extern const u8* Port_GetSpriteAnimationData(unsigned short, unsigned int);
    {
        u32 ids[2] = { 0x22, 0x28 };
        const char* nms[2] = { "SMITH", "ZELDA" };
        for (int i = 0; i < 2; i++) {
            Entity* p = DeepFindEntityByID(NPC, ids[i]);
            if (!p) {
                fprintf(stderr, "[introdbg]   %s(0x%02x): NOT SPAWNED\n", nms[i], ids[i]);
                continue;
            }
            unsigned ai = (p->animIndex == 0xFF) ? 0 : p->animIndex;
            const u8* anim = Port_GetSpriteAnimationData((unsigned short)p->spriteIndex, ai);
            fprintf(stderr,
                    "[introdbg]   %s(0x%02x) flags=0x%02x action=%u scripted=%d draw=%u sprIdx=%d "
                    "animIdx=%u frameIdx=%u animPtr=%p animResolved=%p\n",
                    nms[i], p->id, p->flags, p->action, (p->flags & ENT_SCRIPTED) ? 1 : 0,
                    (unsigned)p->spriteSettings.draw, (int)p->spriteIndex, (unsigned)p->animIndex,
                    (unsigned)p->frameIndex, p->animPtr, (const void*)anim);
        }
    }
}

void Port_ReproIntroDbg_Tick(unsigned int frame) {
    static int active = -1;
    static int started = 0;
    static unsigned int lastSync = 0xFFFFFFFFu;

    if (active < 0) {
        const char* env = getenv("TMC_REPRO_INTRODBG");
        active = (env && *env && strcmp(env, "0") != 0) ? 1 : 0;
        if (active)
            fprintf(stderr, "[introdbg] harness active (natural new-game intro via input injection)\n");
    }
    if (!active)
        return;

    /* One-shot: prove the intro script Call targets resolve under the ACTIVE
     * region. The intro bytecode is read from the active ROM, which contains the
     * region's own Call addresses; Port_LookupScriptFunc must map them. We probe
     * BOTH the USA and JP encodings of script_PlayerIntro / script_SmithIntro2's
     * Call targets so the log shows which region's addresses resolve. */
    {
        static int probed = 0;
        if (!probed && frame == 2) {
            probed = 1;
            extern void* Port_LookupScriptFunc(unsigned int gba_addr);
            struct { const char* nm; unsigned usa, jp; } k[] = {
                { "DisablePauseMenuAndPutAwayItems", 0x0807df29, 0x0807dd65 },
                { "sub_08053250",                    0x08053251, 0x080530d5 },
                { "SetPriorityMessage",              0x0807f349, 0x0807f185 },
                { "EnablePauseMenu",                 0x0807df51, 0x0807dd8d },
                { "ResetPlayerAnimationAndAction",   0x080791d1, 0x0807900d },
            };
            for (unsigned i = 0; i < sizeof(k)/sizeof(k[0]); i++)
                fprintf(stderr, "[introprobe] %-32s USA(0x%08x)=%p  JP(0x%08x)=%p\n",
                        k[i].nm, k[i].usa, Port_LookupScriptFunc(k[i].usa),
                        k[i].jp, Port_LookupScriptFunc(k[i].jp));
        }
    }

    /* Log every task transition so we can see where the flow stalls. */
    {
        static unsigned lastTask = 0xFF;
        if (gMain.task != lastTask) {
            fprintf(stderr, "[introdbg] frame=%u TASK %u -> %u (state=%u)\n", frame,
                    (unsigned)lastTask, (unsigned)gMain.task, (unsigned)gMain.state);
            fflush(stderr);
            lastTask = gMain.task;
        }
    }

    /* Title screen: tap START to advance to file-select.
     * Edge-triggered menus need a release between presses: press for 2 frames
     * out of every 8 (so newKeys sees a clean rising edge each cycle). */
    if (gMain.task == TASK_TITLE && (frame % 8) < 2) {
        IntroDbg_PressKey(KEY_START);
    }

    /* File-select: spam A (clean edges, 2-of-8) through the whole vanilla new-game
     * flow — pick empty slot 0, accept the default name, confirm "start game" —
     * letting the engine's real new-game init run (FinalizeSave sets area 0x22
     * room 0x15, Link's bedroom) so the intro cutscene plays naturally. */
    if (gMain.task == TASK_FILE_SELECT && (frame % 8) < 2) {
        IntroDbg_PressKey(KEY_A);
    }

    if (gMain.task == TASK_GAME && !started) {
        started = 1;
        fprintf(stderr, "[introdbg] frame %u: entered TASK_GAME (area=0x%02x room=0x%02x)\n",
                frame, (unsigned)gRoomControls.area, (unsigned)gRoomControls.room);
    }

    /* Log control mode + syncFlags every change, plus a periodic heartbeat. */
    if (started) {
        if (gActiveScriptInfo.syncFlags != lastSync) {
            fprintf(stderr, "[introdbg] frame=%u SYNCFLAGS 0x%08X -> 0x%08X (ctrl=%u)\n",
                    frame, lastSync, (unsigned)gActiveScriptInfo.syncFlags,
                    (unsigned)gPlayerState.controlMode);
            lastSync = gActiveScriptInfo.syncFlags;
        }
        if (frame % 60 == 0) {
            extern Message gMessage;
            fprintf(stderr,
                    "[introdbg] frame=%u task=%u area=0x%02x room=0x%02x ctrlMode=%u syncFlags=0x%08X msgState=0x%02x msgIdx=%u\n",
                    frame, (unsigned)gMain.task, (unsigned)gRoomControls.area,
                    (unsigned)gRoomControls.room, (unsigned)gPlayerState.controlMode,
                    (unsigned)gActiveScriptInfo.syncFlags,
                    (unsigned)gMessage.state, (unsigned)gMessage.textIndex);
            IntroDbg_DumpNpcs();
        }
    }

    if (frame >= 5000) {
        fprintf(stderr, "[introdbg] done (task=%u ctrlMode=%u syncFlags=0x%08X)\n",
                (unsigned)gMain.task, (unsigned)gPlayerState.controlMode,
                (unsigned)gActiveScriptInfo.syncFlags);
        fflush(stderr);
        _Exit(0);
    }
}
