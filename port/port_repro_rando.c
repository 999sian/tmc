/*
 * port/port_repro_rando.c — headless end-to-end randomizer check (TMC_REPRO_RANDO=1).
 *
 * Drives the same code paths the file-select overlay and the engine reward
 * hooks use, then asserts that items actually change:
 *   1. Open the file-select randomizer overlay (Port_RandoFileMenu_Open).
 *   2. Drive it through the same state-mutation helpers the ImGui modal
 *      calls (Port_RandoFileMenu_SetSeed / CommitAndStart / ...).
 *   3. Confirm "Generate & Start" produced an active seed matching the typed
 *      seed string, and that the engine's central item-override hook
 *      (Rando_OverrideItem — the one GiveItemWithCutscene calls for every
 *      chest/NPC/drop) now remaps at least one item.
 *   4. Repeat with an in-memory public-format .logic file so the
 *      location-key hook (Rando_OverrideLocationKey) is exercised too.
 *
 * Combine with TMC_AUTOPLAY=1 + SDL_VIDEODRIVER=dummy for a headless run.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "rando/rando.h"
#include "rando/rando_file_menu.h"
#include "rando/rando_logic.h"
#include "room.h"
#include "rando/rando_save.h"
#include "rando/rando_runtime.h"
#include "save.h"  /* gSave — world-open stage asserts flag/kinstone bytes */
#include "flags.h" /* CheckGlobalFlag / CheckLocalFlagByBank + flag enums */
#include "main.h"  /* gMain, TASK_GAME — homewarp stage gates on gameplay */
#include "port_gba_mem.h" /* gIoMem — KEYINPUT presses for the homewarp stage */

extern bool Rando_OverrideLocationKey(uint32_t location_key, unsigned char* type, unsigned char* subtype);
extern bool Port_ImGui_CanPresent(void);

static int item_changes(uint16_t lo, uint16_t hi) {
    int changed = 0;
    for (unsigned t = lo; t <= hi; ++t) {
        unsigned char a = (unsigned char)t;
        unsigned char b = 0;
        if (Rando_OverrideItem(&a, &b) && a != (unsigned char)t) {
            changed++;
        }
    }
    return changed;
}

static int item_changed(uint16_t id) {
    unsigned char a = (unsigned char)id;
    unsigned char b = 0;
    return Rando_OverrideItem(&a, &b) && a != (unsigned char)id;
}

static int run_menu_path(void) {
    /* The overlay starts focused on the seed text field with "MINISH". */
    Port_RandoFileMenu_Open(0);
    if (!Port_RandoFileMenu_IsOpen()) {
        fprintf(stderr, "[rando-repro] FAIL: overlay did not open\n");
        return 0;
    }

    /* Type a deterministic numeric seed and confirm — the same helpers the
     * ImGui modal's seed field and "Generate Seed & Start Game" button call. */
    Port_RandoFileMenu_SetSeed("12345");
    Port_RandoFileMenu_CommitAndStart();

    if (Port_RandoFileMenu_IsOpen()) {
        fprintf(stderr, "[rando-repro] FAIL: overlay still open after confirm\n");
        return 0;
    }
    if (!Rando_IsActive()) {
        fprintf(stderr, "[rando-repro] FAIL: no active seed after Generate & Start\n");
        return 0;
    }
    if (Rando_GetSeed64() != Rando_SeedFromString("12345")) {
        fprintf(stderr, "[rando-repro] FAIL: active seed %llu != typed seed %llu\n",
                (unsigned long long)Rando_GetSeed64(),
                (unsigned long long)Rando_SeedFromString("12345"));
        return 0;
    }

    /* NORMAL pool: collectibles shuffle (visible in-game), progression is
     * left intact so the seed stays beatable. Smith Sword (0x01) and Gust Jar
     * (0x11) are progression — must be unchanged under NORMAL. */
    int changed = item_changes(0x54, 0x6F); /* rupee/kinstone/heart/ammo range */
    if (changed <= 0) {
        fprintf(stderr, "[rando-repro] FAIL: NORMAL seed shuffled 0 collectibles\n");
        return 0;
    }
    if (item_changed(0x11) || item_changed(0x01)) {
        fprintf(stderr, "[rando-repro] FAIL: NORMAL altered progression (not beatable-safe)\n");
        return 0;
    }
    fprintf(stderr, "[rando-repro] menu path OK: seed=%llu, %d collectibles shuffled, progression intact\n",
            (unsigned long long)Rando_GetSeed64(), changed);

    /* CHAOS must additionally shuffle the progression pool. Since the
     * #155 glitchless guard, the HARD/CHAOS extra pools only apply with
     * glitchless OFF (glitchless ON keeps majors/progression vanilla in
     * the bijection), so the stage runs glitchless-off explicitly — and
     * first proves glitchless ON really does pin progression. */
    {
        RandomizerSettings chaos = Rando_DefaultSettings();
        chaos.item_difficulty = RANDO_ITEM_POOL_CHAOS;
        chaos.glitchless_logic = true;
        if (!GenerateSeed(0x99, chaos) || !Rando_IsActive()) {
            fprintf(stderr, "[rando-repro] FAIL: CHAOS+glitchless generation failed\n");
            return 0;
        }
        if (item_changes(0x11, 0x17) != 0) {
            fprintf(stderr, "[rando-repro] FAIL: CHAOS+glitchless moved progression\n");
            return 0;
        }
        chaos.glitchless_logic = false;
        if (!GenerateSeed(0x99, chaos) || !Rando_IsActive()) {
            fprintf(stderr, "[rando-repro] FAIL: CHAOS generation failed\n");
            return 0;
        }
        int prog = item_changes(0x11, 0x17); /* gust jar..ocarina */
        if (prog <= 0) {
            fprintf(stderr, "[rando-repro] FAIL: CHAOS shuffled 0 progression items\n");
            return 0;
        }
        fprintf(stderr, "[rando-repro] chaos OK: %d progression items shuffled\n", prog);
    }
    return 1;
}

static int run_logic_key_path(void) {
    /* Minimal .logic text: one Major item, one keyed Major
     * location, plus filler. The Major item must land on the keyed chest. */
    static const char kLogic[] =
        "Items.GustJar; Major;\n"
        "Items.Rupees20; Filler;\n"
        "KeyedChest; Major; 11-22-33; ;\n";
    if (!RandoLogic_LoadText(kLogic, sizeof(kLogic) - 1)) {
        RandoLogicStats st = RandoLogic_GetStats();
        fprintf(stderr, "[rando-repro] FAIL: logic parse: %s\n", st.error);
        return 0;
    }

    RandomizerSettings settings = Rando_DefaultSettings();
    if (!GenerateSeed(0xABCDEF, settings) || !Rando_IsActive()) {
        fprintf(stderr, "[rando-repro] FAIL: logic generation failed\n");
        RandoLogic_Reset();
        return 0;
    }

    unsigned char type = 0x99; /* vanilla placeholder */
    unsigned char sub = 0;
    bool overridden = Rando_OverrideLocationKey(0x112233u, &type, &sub);
    RandoLogic_Reset();
    Rando_Reset();

    if (!overridden) {
        fprintf(stderr, "[rando-repro] FAIL: location-key hook did not fire for 0x112233\n");
        return 0;
    }
    fprintf(stderr, "[rando-repro] logic-key path OK: chest 0x112233 -> item 0x%02X\n", type);
    return 1;
}

/* Issue #155 world-opening stage: a synthetic .logic with the speed-up
 * eventdefines drives Rando_Runtime_OnNewFile() and asserts the engine
 * state it must produce — story-skip globals (always on for a rando
 * file), `m<hex>` save pokes against their proven gSave anchors, the
 * named open flags, and the homewarp/tingle runtime getters. */
static int run_world_open_test(void) {
    static const char kLogic[] =
        "!eventdefine - mC81 - 0xFE\n"          /* fusedKinstones[0] */
        "!eventdefine - mCBF - 0x40\n"          /* flags[0x23] bit 6 */
        "!eventdefine - m100 - 0xAA\n"          /* below gSave: must skip */
        "!eventdefine - mFFFFF - 0xAA\n"        /* past gSave: must skip */
        "!eventdefine - goldTornado\n"
        "!eventdefine - openWindTribe\n"
        "!eventdefine - openTingleBrothers\n"
        "!eventdefine - openLibrary\n"
        "!eventdefine - allowHomewarp\n"
        "!eventdefine - redCrenelBeanstalk\n"
        "Items.GustJar; Major;\n"
        "Items.Rupees20; Filler;\n"
        "KeyedChest; Major; 11-22-33; ;\n";

    if (!RandoLogic_LoadText(kLogic, sizeof(kLogic) - 1)) {
        fprintf(stderr, "[rando-repro] FAIL: world-open logic parse: %s\n",
                RandoLogic_GetStats().error);
        return 0;
    }
    RandomizerSettings settings = Rando_DefaultSettings();
    if (!GenerateSeed(0x5EEDu, settings) || !Rando_IsActive()) {
        fprintf(stderr, "[rando-repro] FAIL: world-open generation failed\n");
        RandoLogic_Reset();
        return 0;
    }

    Rando_Runtime_OnNewFile();

    int ok = 1;
    /* Story skip is unconditional for a rando new file. */
    if (!CheckGlobalFlag(START) || !CheckGlobalFlag(EZERO_1ST) ||
        !CheckGlobalFlag(TABIDACHI)) {
        fprintf(stderr, "[rando-repro] FAIL: story-skip globals not set\n");
        ok = 0;
    }
    /* Named world-open flags from the eventdefines. */
    if (!CheckGlobalFlag(KUMOTATSUMAKI) || !CheckGlobalFlag(WARP_EVENT_END) ||
        !CheckGlobalFlag(TINGLE_TALK1ST) || !CheckGlobalFlag(MIZUKAKI_START)) {
        fprintf(stderr, "[rando-repro] FAIL: world-open globals not set\n");
        ok = 0;
    }
    if (!CheckLocalFlagByBank(FLAG_BANK_1, BEANDEMO_00)) {
        fprintf(stderr, "[rando-repro] FAIL: beanstalk flag not set\n");
        ok = 0;
    }
    /* `m` pokes land on their gSave anchors (pokes run before the flag
     * setters, so test bit membership where banks share the byte). */
    if (gSave.kinstones.fusedKinstones[0] != 0xFE) {
        fprintf(stderr, "[rando-repro] FAIL: mC81 poke missed (got 0x%02X)\n",
                gSave.kinstones.fusedKinstones[0]);
        ok = 0;
    }
    if ((gSave.flags[0x23] & 0x40) == 0) {
        fprintf(stderr, "[rando-repro] FAIL: mCBF poke missed (got 0x%02X)\n",
                gSave.flags[0x23]);
        ok = 0;
    }
    /* Runtime getters consumed by the pause menu + roomInit sites. */
    if (!Rando_Runtime_AllowHomewarp() || !Rando_Runtime_OpenTingleBrothers()) {
        fprintf(stderr, "[rando-repro] FAIL: homewarp/tingle getters wrong\n");
        ok = 0;
    }
    if (ok) {
        fprintf(stderr,
                "[rando-repro] world-open OK: story skip + pokes + speed-up flags applied\n");
    }
    RandoLogic_Reset();
    Rando_Reset();
    return ok;
}

/* When a real .logic file was loaded at startup (TMC_RANDO_LOGIC), prove the
 * generated per-location table maps to real engine chests: for each logic
 * location key (area-room-chest), find the matching chest TileEntity in that
 * room and confirm the reward override fires. */
static int run_real_logic_chest_probe(void) {
    RandomizerSettings s = Rando_DefaultSettings();
    if (!GenerateSeed(0xABCDu, s) || !Rando_IsActive()) {
        fprintf(stderr, "[rando-repro] FAIL: real-logic generation failed\n");
        return 0;
    }
    uint32_t n = RandoLogic_GetLocationCountRaw();
    int keyed = 0, engine_match = 0, overridden = 0, hook_key_ok = 0;
    uint32_t demo_key = 0xFFFFFFFFu;
    unsigned char demo_vanilla = 0, demo_item = 0;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t key = RandoLogic_GetLocationKeyAt(i);
        if (key == 0xFFFFFFFFu) continue;
        keyed++;
        u32 area = (key >> 16) & 0xff, room = (key >> 8) & 0xff, chest = key & 0xff;
        u8 vanilla = 0xFF;
        if (area < 0x90 && room < 64) {
            TileEntity* te = (TileEntity*)GetRoomProperty(area, room, 3);
            int idx = 0;
            for (int k = 0; te != NULL && k < 256 && te[k].type != 0; ++k) {
                if (te[k].type != SMALL_CHEST && te[k].type != BIG_CHEST) continue;
                if (idx == (int)chest) {
                    engine_match++;
                    vanilla = te[k]._2;
                    /* Exact runtime keying function used by OpenSmallChest /
                     * the big-chest hook: localFlag -> room chest index. It must
                     * reproduce the logic's chest-index byte. */
                    if (Rando_RoomChestIndex(area, room, te[k].localFlag) == (int)chest) hook_key_ok++;
                    break;
                }
                idx++;
            }
        }
        /* Replicate the exact in-game small-chest hook (playerItemUtils.c):
         * vanilla item -> Rando_OverrideLocationKey(area-room-chestIndex). */
        unsigned char t = vanilla, sub = 0;
        if (Rando_OverrideLocationKey(key, &t, &sub)) {
            overridden++;
            if (demo_key == 0xFFFFFFFFu && vanilla != 0xFF && t != vanilla) {
                demo_key = key; demo_vanilla = vanilla; demo_item = t;
            }
        }
    }
    fprintf(stderr, "[rando-repro] real-chest probe: keyed=%d engine-chest-match=%d overridden=%d hook-key-ok=%d\n",
            keyed, engine_match, overridden, hook_key_ok);
    if (demo_key != 0xFFFFFFFFu) {
        fprintf(stderr, "[rando-repro]   e.g. chest area %02X room %02X #%u: vanilla 0x%02X -> 0x%02X\n",
                (demo_key >> 16) & 0xff, (demo_key >> 8) & 0xff, demo_key & 0xff, demo_vanilla, demo_item);
    }
    if (engine_match <= 0 || overridden <= 0 || hook_key_ok != engine_match) return 0;

    /* Persistence round-trip: a real-logic seed must survive sidecar
     * save/reload (so chests stay randomized across save+restart). Capture a
     * known override, save, reset only the active seed (keep the loaded
     * logic), reload, and confirm the same chest resolves to the same item. */
    uint32_t sample_key = 0xFFFFFFFFu;
    unsigned char want = 0;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t key = RandoLogic_GetLocationKeyAt(i);
        if (key == 0xFFFFFFFFu) continue;
        unsigned char t = 0xEE, sub = 0;
        if (Rando_OverrideLocationKey(key, &t, &sub)) { sample_key = key; want = t; break; }
    }
    if (!Port_RandoSave_SaveActiveSlot(0)) {
        fprintf(stderr, "[rando-repro] FAIL: sidecar save failed\n");
        return 0;
    }
    Rando_Reset();
    if (Rando_IsActive()) {
        fprintf(stderr, "[rando-repro] FAIL: reset did not clear active seed\n");
        return 0;
    }
    if (!Port_RandoSave_LoadSlot(0) || !Rando_IsActive()) {
        fprintf(stderr, "[rando-repro] FAIL: sidecar reload failed\n");
        return 0;
    }
    unsigned char t2 = 0xEE, sub2 = 0;
    if (!Rando_OverrideLocationKey(sample_key, &t2, &sub2) || t2 != want) {
        fprintf(stderr, "[rando-repro] FAIL: chest 0x%06X gave 0x%02X before, 0x%02X after reload\n",
                sample_key, want, t2);
        return 0;
    }
    fprintf(stderr, "[rando-repro] persistence OK: chest 0x%06X -> 0x%02X survives sidecar reload\n",
            sample_key, t2);
    return 1;
}

/* Drive the file-select overlay in logic mode: toggle a known logic setting
 * through simulated input and confirm it actually re-parsed and changed the
 * generated pool (proves the menu drives real-logic generation). */
static int run_real_logic_menu_test(void) {
    Port_RandoFileMenu_Open(0);
    if (!Port_RandoFileMenu_IsOpen()) {
        fprintf(stderr, "[rando-repro] FAIL: overlay did not open (logic mode)\n");
        return 0;
    }
    int fig = -1;
    uint32_t sc = RandoLogic_GetSettingCount();
    for (uint32_t i = 0; i < sc; ++i) {
        const RandoLogicSetting* s = RandoLogic_GetSetting(i);
        if (s != NULL && strcmp(s->define, "FIGURINE_HUNT") == 0) { fig = (int)i; break; }
    }
    if (fig < 0) {
        fprintf(stderr, "[rando-repro] FAIL: FIGURINE_HUNT setting not found\n");
        Port_RandoFileMenu_Close();
        return 0;
    }
    uint32_t base = RandoLogic_GetStats().item_count;
    /* Toggle the flag through the modal's mutation helper -> override + reparse. */
    Port_RandoFileMenu_ChangeLogicSetting(fig, +1);
    uint32_t after = RandoLogic_GetStats().item_count;
    Port_RandoFileMenu_Close();
    RandoLogic_ClearOverrides();
    RandoLogic_Reparse();
    if (after <= base) {
        fprintf(stderr, "[rando-repro] FAIL: menu toggle did not change pool (%u -> %u)\n", base, after);
        return 0;
    }
    fprintf(stderr, "[rando-repro] menu settings OK: toggling FIGURINE_HUNT changed pool %u -> %u\n", base, after);
    return 1;
}

/* Stage 2: drive the REAL ImGui modal with synthetic SDL key events.
 * Unlike the frame-200 tests (which call the state helpers directly),
 * this exercises the user-visible keyboard path end-to-end: SDL event
 * pump -> ImGui_ImplSDL3 -> nav focus / EnterReturnsTrue / window-level
 * Enter handler -> CommitAndStart. Port_ReproRando_Tick runs before the
 * port_bios input mask, so the harness keeps ticking while the modal is
 * open exactly like a player at the screen. */
extern SDL_Window* Port_PPU_ActiveWindow(void);

static void push_return_key(bool down) {
    SDL_Event e;
    SDL_zero(e);
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.timestamp = SDL_GetTicksNS();
    /* ImGui_ImplSDL3_ProcessEvent drops key events whose windowID doesn't
     * match its window — stamp the real one. */
    e.key.windowID = SDL_GetWindowID(Port_PPU_ActiveWindow());
    e.key.scancode = SDL_SCANCODE_RETURN;
    e.key.key = SDLK_RETURN;
    e.key.down = down;
    SDL_PushEvent(&e);
}

#define REPRO_IMGUI_OPEN_FRAME 240
#define REPRO_IMGUI_DEADLINE   480

static int imgui_keyboard_stage(unsigned int frame, int* done) {
    static int presses = 0;
    *done = 0;

    if (frame == REPRO_IMGUI_OPEN_FRAME) {
        Port_RandoFileMenu_Open(0);
        Port_RandoFileMenu_SetSeed("424242");
        if (!Port_RandoFileMenu_IsOpen()) {
            fprintf(stderr, "[rando-repro] FAIL: imgui stage overlay did not open\n");
            *done = 1;
            return 0;
        }
        return 1;
    }

    if (!Port_RandoFileMenu_IsOpen()) {
        /* Modal closed: Enter must have committed. Verify the seed went
         * active and matches the typed text. */
        if (!Rando_IsActive() ||
            Rando_GetSeed64() != Rando_SeedFromString("424242")) {
            fprintf(stderr,
                    "[rando-repro] FAIL: imgui Enter commit seed mismatch "
                    "(active=%d seed=%llu want=%llu)\n",
                    Rando_IsActive() ? 1 : 0,
                    (unsigned long long)Rando_GetSeed64(),
                    (unsigned long long)Rando_SeedFromString("424242"));
            *done = 1;
            return 0;
        }
        fprintf(stderr,
                "[rando-repro] imgui keyboard path OK: Enter committed after "
                "%d press(es), seed matches\n", presses);
        *done = 1;
        return 1;
    }

    /* Tap Return every 12 frames (down for 3) until the modal commits.
     * Depending on nav focus the first press may only start editing the
     * seed field; the next one then commits via EnterReturnsTrue. */
    if ((frame % 12) == 0) {
        push_return_key(true);
        presses++;
    } else if ((frame % 12) == 3) {
        push_return_key(false);
    }

    if (frame >= REPRO_IMGUI_DEADLINE) {
        fprintf(stderr,
                "[rando-repro] FAIL: imgui modal still open after %d Enter "
                "presses\n", presses);
        *done = 1;
        return 0;
    }
    return 1;
}

/* Stage 3 (real-logic runs only): end-to-end homewarp. Drives title +
 * file-select with GBA KEYINPUT presses (same pattern as
 * port_repro_a11y.c), committing the auto-opened rando modal so the new
 * rando file actually boots into gameplay. Then: warp away from home,
 * arm Rando_Homewarp_Request() (the same call the pause menu makes on
 * SELECT), and assert the deferred tick lands Link back at his bed
 * (area 0x22 room 0x15). */
extern int Port_DebugAction_Warp(unsigned char area, unsigned char room,
                                 unsigned short x, unsigned short y, unsigned char layer);

#define REPRO_KEYINPUT_REG 0x130
#define REPRO_A_BUTTON 0x0001
#define REPRO_START_BUTTON 0x0008

/* Port_ReproRando_Tick runs BEFORE Port_UpdateInput's KEYINPUT store (so
 * the ImGui stage can tick under the modal input mask) — a direct poke
 * from the homewarp stage would be overwritten before the game reads
 * it. The stage queues presses here; Port_ReproRando_LateTick (called
 * after the store, with the other repro ticks) applies them. */
static unsigned short sLatePressMask = 0;

void Port_ReproRando_LateTick(void) {
    if (sLatePressMask != 0) {
        *(volatile unsigned short*)(gIoMem + REPRO_KEYINPUT_REG) &=
            (unsigned short)~sLatePressMask;
        sLatePressMask = 0;
    }
}

static int homewarp_stage(unsigned int frame, int* done) {
    static int state = 0;
    static unsigned int t0 = 0;
    *done = 0;

    if (t0 == 0) {
        t0 = frame;
        fprintf(stderr, "[rando-repro] homewarp stage: driving into gameplay\n");
    }
    if (frame - t0 > 3000) {
        fprintf(stderr, "[rando-repro] FAIL: homewarp stage timed out (state %d, task %u, area %02X room %02X)\n",
                state, (unsigned)gMain.task, gRoomControls.area, gRoomControls.room);
        *done = 1;
        return 0;
    }

    switch (state) {
        case 0: /* title -> file select -> new game (KEYINPUT is active-low) */
            if (gMain.task == 0 && frame >= 30 && (frame & 0xF) < 3) {
                sLatePressMask |= REPRO_START_BUTTON;
            }
            if (gMain.task == 1) {
                if (Port_RandoFileMenu_IsOpen()) {
                    /* The setup modal auto-opened for the new file; commit
                     * it programmatically (it masks game input while open). */
                    if ((frame & 0x1F) == 0) {
                        Port_RandoFileMenu_CommitAndStart();
                    }
                } else if ((frame & 0xF) < 3) {
                    sLatePressMask |= REPRO_A_BUTTON;
                }
            }
            if (gMain.task == TASK_GAME && gRoomControls.area == 0x22) {
                fprintf(stderr, "[rando-repro] homewarp stage: in gameplay (new file at home)\n");
                state = 1;
                t0 = frame;
            }
            return 1;
        case 1: /* let the room settle, then warp away from home */
            if (frame - t0 < 60) {
                return 1;
            }
            if (!Port_DebugAction_Warp(0x03, 0x01, 0x290, 0x19C, 1)) {
                return 1; /* not accepted yet; retry next frame */
            }
            state = 2;
            t0 = frame;
            return 1;
        case 2: /* confirm we left, then request homewarp */
            if (frame - t0 < 90) {
                return 1;
            }
            if (gRoomControls.area != 0x03) {
                return 1; /* still transitioning */
            }
            if (!Rando_Homewarp_Request()) {
                fprintf(stderr, "[rando-repro] FAIL: homewarp request refused\n");
                *done = 1;
                return 0;
            }
            state = 3;
            t0 = frame;
            return 1;
        case 3: /* deferred tick must land us at the bed */
            if (gRoomControls.area == 0x22 && gRoomControls.room == 0x15) {
                fprintf(stderr, "[rando-repro] homewarp OK: warped back to Link's bed\n");
                *done = 1;
                return 1;
            }
            return 1;
    }
    *done = 1;
    return 0;
}

void Port_ReproRando_Tick(unsigned int frame) {
    static int mode = -1;
    static int stage1_ok = 0;
    static int stage2_ok = 0;
    if (mode < 0) {
        const char* v = getenv("TMC_REPRO_RANDO");
        mode = (v && *v && strcmp(v, "0") != 0) ? 1 : 0;
        if (mode) {
            fprintf(stderr, "[rando-repro] enabled; will run at frame 200\n");
        }
    }
    if (!mode) {
        return;
    }

    if (frame == 200) {
        int ok;
        if (RandoLogic_IsLoaded()) {
            /* A real .logic was loaded at startup: every GenerateSeed uses it,
             * so exercise the real per-location chest path instead of the
             * built-in bijection tests. */
            ok = run_real_logic_chest_probe();
            if (ok) ok = run_real_logic_menu_test();
        } else {
            ok = run_menu_path();
            if (ok) ok = run_logic_key_path();
            if (ok) ok = run_world_open_test();
        }
        if (!ok) {
            fprintf(stderr, "[rando-repro] FAIL\n");
            fflush(stderr);
            _Exit(1);
        }
        stage1_ok = 1;
        if (!Port_ImGui_CanPresent()) {
            /* No ImGui this run (surface fallback): the modal never auto-
             * opens there (Port_RandoFileMenu_ShouldOpenForNewFile gates on
             * CanPresent), so the keyboard stage doesn't apply. */
            fprintf(stderr, "[rando-repro] imgui unavailable; skipping keyboard stage\n");
            fprintf(stderr, "[rando-repro] PASS\n");
            fflush(stderr);
            _Exit(0);
        }
        return;
    }

    if (stage1_ok && !stage2_ok && frame >= REPRO_IMGUI_OPEN_FRAME) {
        int done = 0;
        int ok = imgui_keyboard_stage(frame, &done);
        if (done) {
            if (!ok) {
                fprintf(stderr, "[rando-repro] FAIL\n");
                fflush(stderr);
                _Exit(1);
            }
            /* Homewarp needs the `allowHomewarp` eventdefine, i.e. a
             * loaded .logic (HOMEWARP defaults on). Built-in-graph runs
             * have no defines, so they finish here. */
            if (!RandoLogic_IsLoaded() || !Rando_Runtime_AllowHomewarp()) {
                fprintf(stderr, "[rando-repro] homewarp unavailable (no .logic); skipping stage\n");
                fprintf(stderr, "[rando-repro] PASS\n");
                fflush(stderr);
                _Exit(0);
            }
            stage2_ok = 1;
        }
    }

    if (stage2_ok) {
        int done = 0;
        int ok = homewarp_stage(frame, &done);
        if (done) {
            fprintf(stderr, "[rando-repro] %s\n", ok ? "PASS" : "FAIL");
            fflush(stderr);
            _Exit(ok ? 0 : 1);
        }
    }
}
