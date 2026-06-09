/*
 * port/port_repro_rando.c — headless end-to-end randomizer check (TMC_REPRO_RANDO=1).
 *
 * Drives the same code paths the file-select overlay and the engine reward
 * hooks use, then asserts that items actually change:
 *   1. Open the SDL file-select randomizer overlay (Port_RandoFileMenu_Open).
 *   2. Feed it real SDL events (text input + navigation + confirm) through
 *      ProcessFileMenuInput, exactly as port_bios.c routes them.
 *   3. Confirm "Generate & Start" produced an active seed matching the typed
 *      seed string, and that the engine's central item-override hook
 *      (Rando_OverrideItem — the one GiveItemWithCutscene calls for every
 *      chest/NPC/drop) now remaps at least one item.
 *   4. Repeat with an in-memory MinishMaker-style .logic file so the
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

extern bool Rando_OverrideLocationKey(uint32_t location_key, unsigned char* type, unsigned char* subtype);

static void push_key(SDL_Keycode key) {
    SDL_Event e;
    memset(&e, 0, sizeof(e));
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = key;
    e.key.repeat = false;
    ProcessFileMenuInput(&e);
}

static void push_text(const char* text) {
    SDL_Event e;
    memset(&e, 0, sizeof(e));
    e.type = SDL_EVENT_TEXT_INPUT;
    e.text.text = text;
    ProcessFileMenuInput(&e);
}

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

    /* Clear the default seed and type a deterministic numeric seed. */
    for (int i = 0; i < 16; ++i) {
        push_key(SDLK_BACKSPACE);
    }
    push_text("12345");

    /* Navigate from the seed row (index 0) to "Generate & Start" (index 6). */
    for (int i = 0; i < 6; ++i) {
        push_key(SDLK_DOWN);
    }
    push_key(SDLK_RETURN);

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

    /* CHAOS must additionally shuffle the progression pool. */
    {
        RandomizerSettings chaos = Rando_DefaultSettings();
        chaos.item_difficulty = RANDO_ITEM_POOL_CHAOS;
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
    /* Minimal MinishMaker-style logic: one Major item, one keyed Major
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
    /* Row 0 is seed; settings start at row 2. Navigate down to the flag. */
    for (int k = 0; k < 2 + fig; ++k) push_key(SDLK_DOWN);
    push_key(SDLK_RIGHT); /* toggle the flag -> override + reparse */
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

void Port_ReproRando_Tick(unsigned int frame) {
    static int mode = -1;
    if (mode < 0) {
        const char* v = getenv("TMC_REPRO_RANDO");
        mode = (v && *v && strcmp(v, "0") != 0) ? 1 : 0;
        if (mode) {
            fprintf(stderr, "[rando-repro] enabled; will run at frame 200\n");
        }
    }
    if (!mode || frame != 200) {
        return;
    }

    int ok;
    if (RandoLogic_IsLoaded()) {
        /* A real .logic was loaded at startup: every GenerateSeed uses it, so
         * exercise the real per-location chest path instead of the built-in
         * bijection tests. */
        ok = run_real_logic_chest_probe();
        if (ok) ok = run_real_logic_menu_test();
    } else {
        ok = run_menu_path();
        if (ok) ok = run_logic_key_path();
    }

    fprintf(stderr, "[rando-repro] %s\n", ok ? "PASS" : "FAIL");
    fflush(stderr);
    _Exit(ok ? 0 : 1);
}
