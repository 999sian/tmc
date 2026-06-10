#include "rando/rando.h"
#include "rando/rando_logic.h"
#include "rando/rando_keymap.h"
#include "item_ids.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* Port_Save_GetActivePath(void) {
    return "rando_test.sav";
}

static int copy_table(uint16_t* out, size_t cap) {
    memset(out, 0, cap * sizeof(out[0]));
    const uint16_t* table = Rando_GetRandomizedItemTable();
    size_t n = Rando_GetLocationCount();
    if (n > cap) return 0;
    memcpy(out, table, n * sizeof(out[0]));
    return 1;
}

static int run_logic_parser_test(RandomizerSettings settings) {
    static const char kLogicText[] =
        "!dropdown - Main Settings - Setting - Test - TEST_SETTING - Test Setting - - TEST_DEFAULT - Default - TEST_DEFAULT -\n"
        "!define - `TEST_SETTING`\n"
        "!addition - TEST_SUM - 1, 2, 0x3\n"
        "Items.GustJar; Major;\n"
        "Items.EarthElement; Major;\n"
        "Items.Rupees20; Filler;\n"
        "!replaceamount - Items.Rupees50:0:1; - 1 - Items.Rupees20\n"
        "StartChest; Major; 00-00-01; ;\n"
        "TownChest; Major; 00-00-02; ;\n"
        "DeepwoodReward; Any; 00-00-03; Items.GustJar;\n"
        "Goal; Helper; ; Items.EarthElement, Locations.DeepwoodReward;\n";
    uint16_t a[RANDO_LOCATION_COUNT];
    uint16_t b[RANDO_LOCATION_COUNT];

    if (!RandoLogic_LoadText(kLogicText, sizeof(kLogicText) - 1)) {
        RandoLogicStats stats = RandoLogic_GetStats();
        fprintf(stderr, "rando_test: logic parse failed: %s\\n", stats.error);
        return 0;
    }
    RandoLogicStats stats = RandoLogic_GetStats();
    if (!stats.loaded || !stats.native_assignable || stats.item_count != 3 || stats.location_count != 4) {
        fprintf(stderr, "rando_test: unexpected logic stats items=%u locations=%u assignable=%u\\n",
                stats.item_count, stats.location_count, stats.native_assignable ? 1u : 0u);
        return 0;
    }
    if (RandoLogic_FindLocationByKey(0x000001u) < 0) {
        fprintf(stderr, "rando_test: location key lookup failed\n");
        return 0;
    }
    if (!GenerateSeed(0xfeedfaceu, settings) || !Rando_VerifyCurrentSeed() || !copy_table(a, RANDO_LOCATION_COUNT)) {
        fprintf(stderr, "rando_test: parsed logic generation failed\\n");
        return 0;
    }
    if (!GenerateSeed(0xfeedfaceu, settings) || !Rando_VerifyCurrentSeed() || !copy_table(b, RANDO_LOCATION_COUNT)) {
        fprintf(stderr, "rando_test: parsed logic second generation failed\\n");
        return 0;
    }
    if (memcmp(a, b, sizeof(a)) != 0) {
        fprintf(stderr, "rando_test: parsed logic deterministic mismatch\\n");
        return 0;
    }
    RandoLogic_Reset();
    Rando_Reset();
    return 1;
}

static int item_at_key(uint32_t key) {
    uint8_t type = 0x99;
    uint8_t sub = 0;
    if (!Rando_OverrideLocationKey(key, &type, &sub)) return -1;
    return (int)type;
}

static int expect_bound_location(uint32_t key, const char* want_name) {
    int idx = RandoLogic_FindLocationByKey(key);
    if (idx < 0) {
        fprintf(stderr, "[real] FAIL: key 0x%08X not bound\n", key);
        return 0;
    }
    const char* got = RandoLogic_GetLocationName((uint32_t)idx);
    if (got == NULL || strcmp(got, want_name) != 0) {
        fprintf(stderr, "[real] FAIL: key 0x%08X bound to %s, expected %s\n", key, got ? got : "(null)", want_name);
        return 0;
    }
    return 1;
}

/* Exercises the documented placement semantics:
 *   - `~Items.X` is a placement guard (X cannot land on that location),
 *   - assumed fill keeps a chain of gated locations beatable,
 *   - identical seeds are deterministic. */
static int run_engine_semantics_test(void) {
    static const char kGuard[] =
        "Items.GustJar; Major;\n"
        "Items.Rupees20; Filler;\n"
        "ChestA; Major; 00-00-01; ;\n"
        "ChestB; Major; 00-00-02; (~Items.GustJar);\n";
    static const char kChain[] =
        "Items.GustJar; Major;\n"
        "Items.PacciCane; Major;\n"
        "Items.Rupees20; Filler;\n"
        "Start; Major; 00-00-01; ;\n"
        "GateA; Major; 00-00-02; Items.GustJar;\n"
        "GateB; Major; 00-00-03; (& Items.GustJar, Items.PacciCane);\n"
        "Goal; Helper; ; (& Items.GustJar, Items.PacciCane);\n";
    RandomizerSettings s = Rando_DefaultSettings();
    uint16_t a[RANDO_LOCATION_COUNT];
    uint16_t b[RANDO_LOCATION_COUNT];

    /* NOT-guard: GustJar (0x11) must avoid ChestB, so it lands on ChestA and
     * ChestB gets filler (0x56). */
    if (!RandoLogic_LoadText(kGuard, sizeof(kGuard) - 1) || !GenerateSeed(0x1234u, s)) {
        fprintf(stderr, "rando_test: guard logic generation failed\n");
        return 0;
    }
    if (item_at_key(0x000001u) != 0x11) {
        fprintf(stderr, "rando_test: NOT-guard mis-placed progression item\n");
        return 0;
    }
    if (item_at_key(0x000002u) == 0x11) {
        fprintf(stderr, "rando_test: NOT-guard failed (forbidden item placed)\n");
        return 0;
    }
    RandoLogic_Reset();
    Rando_Reset();

    /* Assumed-fill chain: every seed must be beatable (engine refuses to
     * activate otherwise) and identical seeds must be deterministic. */
    if (!RandoLogic_LoadText(kChain, sizeof(kChain) - 1) || !GenerateSeed(0x5151u, s) ||
        !copy_table(a, RANDO_LOCATION_COUNT)) {
        fprintf(stderr, "rando_test: assumed-fill generation failed\n");
        return 0;
    }
    if (!GenerateSeed(0x5151u, s) || !copy_table(b, RANDO_LOCATION_COUNT) ||
        memcmp(a, b, sizeof(a)) != 0) {
        fprintf(stderr, "rando_test: assumed-fill not deterministic\n");
        return 0;
    }
    RandoLogic_Reset();
    Rando_Reset();
    return 1;
}

/* Parity semantics introduced for MinishMaker 1:1: dungeon-id tag binding,
 * `!prizeplacement` redirects, `!eventdefine` evaluation (incl. RAND_INT
 * determinism), and entrance-assignment recording. */
static int run_parity_semantics_test(void) {
    /* DWSKey is tagged DWSSmall: only DWSChest carries that tag, so the key
     * MUST land there even though both chests accept DungeonMajor items.
     * The prize redirects from Pedestal into the :DWSPrize pool (DWSChest2).
     * Entrance dummies pair off against the two entrance locations. */
    static const char kLogicText[] =
        "!eventdefine - dmgMulti - 2\n"
        "!eventdefine - flagOnly\n"
        "!eventdefine - rngVal - 0x`RAND_INT`\n"
        "!eventdefine - masked - ((0x7FFF >> 5) & 0x1F)\n"
        "!prizeplacement - Pedestal - DWSPrize\n"
        "Items.SmallKey.0x18; DungeonMajor; DWSSmall\n"
        "Items.EarthElement; DungeonPrize;\n"
        "Items.Entrance.0x01; DungeonEntrance;\n"
        "Items.Entrance.0x02; DungeonEntrance;\n"
        "Items.Rupees20:2; Filler;\n"
        "DWSChest:DWSSmall:DWSPrize; Dungeon; 00-00-01; ;\n"
        "CoFChest:CoFSmall; Dungeon; 00-00-02; ;\n"
        "DWSChest2:DWSPrize; Dungeon; 00-00-03; ;\n"
        "Pedestal; DungeonPrize; 00-00-04; ;\n"
        "EntranceA; DungeonEntrance; ; ;\n"
        "EntranceB; DungeonEntrance; ; ;\n"
        "Goal; Helper; ; Items.SmallKey.0x18, Items.EarthElement;\n";

    RandomizerSettings settings = Rando_DefaultSettings();
    if (!RandoLogic_LoadText(kLogicText, sizeof(kLogicText) - 1)) {
        fprintf(stderr, "rando_test: parity logic parse failed: %s\n", RandoLogic_GetStats().error);
        return 0;
    }
    RandoLogicStats stats = RandoLogic_GetStats();
    if (stats.tag_count == 0 || stats.prize_rule_count != 1 || stats.eventdefine_count != 4) {
        fprintf(stderr, "rando_test: parity stats wrong tags=%u prize=%u event=%u\n",
                stats.tag_count, stats.prize_rule_count, stats.eventdefine_count);
        return 0;
    }
    if (!GenerateSeed(0xc0ffee123ull, settings)) {
        fprintf(stderr, "rando_test: parity generation failed\n");
        return 0;
    }
    /* Tag binding: the DWS key must be at DWSChest (key 00-00-01), never at
     * the untagged-for-DWSSmall CoFChest. */
    int at_dws = item_at_key(0x000001u);
    int at_cof = item_at_key(0x000002u);
    if (at_dws != ITEM_SMALL_KEY) {
        fprintf(stderr, "rando_test: tagged key not in own dungeon (dws=%d cof=%d)\n", at_dws, at_cof);
        return 0;
    }
    /* Prize redirect: EarthElement must be at DWSChest2 (the only open
     * :DWSPrize slot — DWSChest already holds the key), not at Pedestal. */
    if (item_at_key(0x000003u) != ITEM_EARTH_ELEMENT) {
        fprintf(stderr, "rando_test: prize redirect missed DWSPrize pool (got %d)\n",
                item_at_key(0x000003u));
        return 0;
    }
    if (item_at_key(0x000004u) == ITEM_EARTH_ELEMENT) {
        fprintf(stderr, "rando_test: prize stayed on redirected pedestal\n");
        return 0;
    }
    /* Entrance assignments: both entrance locations got a distinct dummy. */
    {
        int seen1 = 0, seen2 = 0;
        for (uint32_t l = 0; l < RandoLogic_GetLocationCountRaw(); ++l) {
            int e = RandoLogic_GetEntranceAssignment(l);
            if (e == 0x01) seen1++;
            if (e == 0x02) seen2++;
        }
        if (seen1 != 1 || seen2 != 1) {
            fprintf(stderr, "rando_test: entrance assignment wrong (%d/%d)\n", seen1, seen2);
            return 0;
        }
    }
    /* Eventdefines: values evaluate; RAND_INT is per-seed deterministic. */
    {
        bool has_value = false;
        uint32_t v = 0;
        if (!RandoLogic_HasEventDefine("flagOnly", &has_value) || has_value) {
            fprintf(stderr, "rando_test: flag-only eventdefine wrong\n");
            return 0;
        }
        if (!RandoLogic_EvalEventDefine("dmgMulti", 1, &v) || v != 2) {
            fprintf(stderr, "rando_test: dmgMulti eval wrong (%u)\n", v);
            return 0;
        }
        if (!RandoLogic_EvalEventDefine("masked", 1, &v) || v != ((0x7FFFu >> 5) & 0x1Fu)) {
            fprintf(stderr, "rando_test: masked eval wrong (%u)\n", v);
            return 0;
        }
        uint32_t r1 = 0, r2 = 0, r3 = 0;
        if (!RandoLogic_EvalEventDefine("rngVal", 42, &r1) ||
            !RandoLogic_EvalEventDefine("rngVal", 42, &r2) ||
            !RandoLogic_EvalEventDefine("rngVal", 43, &r3)) {
            fprintf(stderr, "rando_test: rngVal eval failed\n");
            return 0;
        }
        if (r1 != r2 || r1 == r3) {
            fprintf(stderr, "rando_test: RAND_INT determinism wrong (%u %u %u)\n", r1, r2, r3);
            return 0;
        }
    }
    RandoLogic_Reset();
    Rando_Reset();
    return 1;
}

/* When TMC_RANDO_LOGIC points at a real .logic file, load it, report parse
 * stats, and require deterministic end-to-end generation of a beatable seed. */
static int run_real_logic_diagnostic(void) {
    const char* path = getenv("TMC_RANDO_LOGIC");
    if (path == NULL || path[0] == '\0') return 1;

    static char buf[2 * 1024 * 1024];
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "[real] cannot open %s\n", path);
        return 1;
    }
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    if (!RandoLogic_LoadText(buf, n)) {
        RandoLogicStats st = RandoLogic_GetStats();
        fprintf(stderr, "[real] FAIL parse: %s\n", st.error);
        return 0;
    }
    RandoLogicStats st = RandoLogic_GetStats();
    fprintf(stderr,
            "[real] loaded items=%u locations=%u helpers=%u symbols=%u nodes=%u defines=%u native=%u\n",
            st.item_count, st.location_count, st.helper_count, st.symbol_count,
            st.node_count, st.define_count, st.native_mapped_items);

    /* Settings control: enabling FIGURINE_HUNT must add figurines to the pool
     * (item count rises after reparse); clearing the override restores it. */
    {
        uint32_t base_items = st.item_count;
        uint32_t setting_count = RandoLogic_GetSettingCount();
        RandoLogic_SetOverride("FIGURINE_HUNT", "true");
        if (!RandoLogic_Reparse()) {
            fprintf(stderr, "[real] FAIL: reparse after override failed\n");
            return 0;
        }
        uint32_t hunt_items = RandoLogic_GetStats().item_count;
        RandoLogic_ClearOverrides();
        RandoLogic_Reparse();
        uint32_t restored_items = RandoLogic_GetStats().item_count;
        fprintf(stderr, "[real] settings=%u; FIGURINE_HUNT: items %u -> %u (restored %u)\n",
                setting_count, base_items, hunt_items, restored_items);
        if (hunt_items <= base_items || restored_items != base_items) {
            fprintf(stderr, "[real] FAIL: setting override did not drive generation\n");
            return 0;
        }
    }
    st = RandoLogic_GetStats();

    static uint16_t a[RANDO_LOCATION_COUNT];
    static uint16_t b[RANDO_LOCATION_COUNT];
    RandomizerSettings s = Rando_DefaultSettings();
    if (!GenerateSeed(0xC0FFEEu, s) || !Rando_IsActive()) {
        fprintf(stderr, "[real] FAIL: could not generate a beatable seed\n");
        RandoLogic_Reset(); Rando_Reset();
        return 0;
    }
    size_t lc = Rando_GetLocationCount();
    memcpy(a, Rando_GetRandomizedItemTable(), lc * sizeof(a[0]));
    if (!GenerateSeed(0xC0FFEEu, s) || !Rando_IsActive()) {
        fprintf(stderr, "[real] FAIL: second generation failed\n");
        RandoLogic_Reset(); Rando_Reset();
        return 0;
    }
    memcpy(b, Rando_GetRandomizedItemTable(), lc * sizeof(b[0]));
    if (memcmp(a, b, lc * sizeof(a[0])) != 0) {
        fprintf(stderr, "[real] FAIL: generation not deterministic\n");
        RandoLogic_Reset(); Rando_Reset();
        return 0;
    }
    fprintf(stderr, "[real] OK: deterministic beatable seed over %u locations\n", (unsigned)lc);
    if (!expect_bound_location(
            RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_STOCKWELL, RANDO_STOCKWELL_SLOT_80, 0, 0),
            "Town_Shop_80Item") ||
        !expect_bound_location(RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_DOJO, 4, 0, 0),
                               "Crenel_Dojo_NPC") ||
        !expect_bound_location(
            RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_CARLOV_MEDAL, 0, 0),
            "Town_Carlov_NPC") ||
        !expect_bound_location(
            RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_DOG_BOTTLE, 0, 0),
            "Hylia_DogNPC") ||
        !expect_bound_location(
            RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_JABBER_NUT, 0, 0),
            "MinishVillage_BarrelHouse_Item") ||
        !expect_bound_location(
            RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_RED_BOOK, 0, 0),
            "Town_Jullieta_Item") ||
        !expect_bound_location(
            RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_GREEN_BOOK, 0, 0),
            "Town_DrLeft_AtticItem") ||
        !expect_bound_location(
            RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_BLUE_BOOK, 0, 0),
            "Hylia_MayorCabin_Item") ||
        !expect_bound_location(
            RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_SPECIAL, RANDO_SPECIAL_KEY_MELARI, 0, 0),
            "Crenel_Melari_NPC")) {
        RandoLogic_Reset(); Rando_Reset();
        return 0;
    }
    fprintf(stderr, "[real] OK: default scripted runtime keys bound for stockwell/dojo/special rewards\n");
    RandoLogic_SetOverride("GORON_SETTING", "GORON_5");
    RandoLogic_SetOverride("BLUE_FUSION_SETTING", "VANILLA_BLUE_FUSIONS");
    RandoLogic_SetOverride("CUCCO_SETTING", "CUCCO_10");
    if (!RandoLogic_Reparse() || !GenerateSeed(0xC0FFEEu, s) || !Rando_IsActive() ||
        !expect_bound_location(
            RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_GORON_MERCHANT, 0, 0, 0),
            "Town_GoronMerchant_1_Left") ||
        !expect_bound_location(RANDO_SCRIPTED_KEY(RANDO_SCRIPTED_KEY_CUCCO, 9, 0, 0),
                               "Town_Cuccos_Lv_10_NPC")) {
        RandoLogic_Reset(); Rando_Reset();
        return 0;
    }
    fprintf(stderr, "[real] OK: opt-in scripted runtime keys bound for goron + cucco rewards\n");
    RandoLogic_ClearOverrides();
    RandoLogic_Reparse();
    if (!GenerateSeed(0xC0FFEEu, s) || !Rando_IsActive()) {
        fprintf(stderr, "[real] FAIL: restore after scripted-key override check failed\n");
        RandoLogic_Reset(); Rando_Reset();
        return 0;
    }
    fprintf(stderr, "[real] OK: scripted runtime keys bound for shop/goron/dojo/cucco/special rewards\n");
    /* MUSIC_RANDO: enabling the flag must yield per-area music assignments
     * (the 192 Area%xMusic locations + Items.Music pool), and they must be
     * deterministic per seed. Default settings must yield none. */
    {
        int base_music = 0;
        for (uint32_t area = 0; area < 256; ++area) {
            if (RandoLogic_GetMusicAssignment(area) >= 0) base_music++;
        }
        if (base_music != 0) {
            fprintf(stderr, "[real] FAIL: music assigned without MUSIC_RANDO (%d)\n", base_music);
            RandoLogic_Reset(); Rando_Reset();
            return 0;
        }
        RandoLogic_SetOverride("MUSIC_SETTING", "MUSIC_RANDO");
        RandoLogic_Reparse();
        if (!GenerateSeed(0xC0FFEEu, s) || !Rando_IsActive()) {
            fprintf(stderr, "[real] FAIL: MUSIC_RANDO generation failed\n");
            RandoLogic_Reset(); Rando_Reset();
            return 0;
        }
        int music_count = 0, first_area = -1, first_song = -1;
        for (uint32_t area = 0; area < 256; ++area) {
            int song = RandoLogic_GetMusicAssignment(area);
            if (song < 0) continue;
            music_count++;
            if (first_area < 0) { first_area = (int)area; first_song = song; }
        }
        if (music_count < 100) {
            fprintf(stderr, "[real] FAIL: MUSIC_RANDO assigned only %d areas\n", music_count);
            RandoLogic_Reset(); Rando_Reset();
            return 0;
        }
        if (!GenerateSeed(0xC0FFEEu, s) ||
            RandoLogic_GetMusicAssignment((uint32_t)first_area) != first_song) {
            fprintf(stderr, "[real] FAIL: music assignment not deterministic\n");
            RandoLogic_Reset(); Rando_Reset();
            return 0;
        }
        fprintf(stderr, "[real] OK: MUSIC_RANDO assigned %d areas (e.g. area 0x%02x -> song 0x%02x)\n",
                music_count, first_area, first_song);
        RandoLogic_ClearOverrides();
        RandoLogic_Reparse();
    }
    {
        static char spoiler[2048];
        size_t sn = Rando_GetSpoiler(spoiler, sizeof(spoiler));
        size_t shown = 0, line = 0;
        for (size_t i = 0; i < sn && line < 6; ++i) {
            if (spoiler[i] == '\n') { fwrite(spoiler + shown, 1, i - shown + 1, stderr); shown = i + 1; line++; }
        }
    }
    RandoLogic_Reset();
    Rando_Reset();
    return 1;
}

int main(void) {
    RandomizerSettings settings = Rando_DefaultSettings();
    uint16_t first[RANDO_LOCATION_COUNT];
    uint16_t second[RANDO_LOCATION_COUNT];
    uint16_t third[RANDO_LOCATION_COUNT];
    const uint64_t seed = 0x123456789abcdef0ull;

    settings.glitchless_logic = true;
    settings.shuffle_kinstones = true;
    settings.shuffle_dojos = true;
    settings.item_difficulty = RANDO_ITEM_POOL_NORMAL;

    if (!GenerateSeed(seed, settings) || !Rando_VerifyCurrentSeed() || !copy_table(first, RANDO_LOCATION_COUNT)) {
        fprintf(stderr, "rando_test: first generation failed\n");
        return 1;
    }
    if (!GenerateSeed(seed, settings) || !Rando_VerifyCurrentSeed() || !copy_table(second, RANDO_LOCATION_COUNT)) {
        fprintf(stderr, "rando_test: second generation failed\n");
        return 1;
    }
    if (memcmp(first, second, sizeof(first)) != 0) {
        fprintf(stderr, "rando_test: deterministic seed mismatch\n");
        return 1;
    }

    settings.shuffle_kinstones = false;
    settings.shuffle_dojos = false;
    settings.item_difficulty = RANDO_ITEM_POOL_HARD;
    if (!GenerateSeed(seed + 1u, settings) || !Rando_VerifyCurrentSeed() || !copy_table(third, RANDO_LOCATION_COUNT)) {
        fprintf(stderr, "rando_test: hard/no-side-pools generation failed\n");
        return 1;
    }
    if (memcmp(first, third, sizeof(first)) == 0) {
        fprintf(stderr, "rando_test: distinct settings produced identical table\n");
        return 1;
    }

    if (!run_logic_parser_test(settings)) {
        return 1;
    }
    if (!run_engine_semantics_test()) {
        return 1;
    }
    if (!run_parity_semantics_test()) {
        return 1;
    }
    if (!run_real_logic_diagnostic()) {
        return 1;
    }

    printf("rando_test: ok (%u locations)\n", (unsigned)Rando_GetLocationCount());
    return 0;
}
