#include "rando/rando.h"
#include "rando/rando_logic.h"

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
    if (!run_real_logic_diagnostic()) {
        return 1;
    }

    printf("rando_test: ok (%u locations)\n", (unsigned)Rando_GetLocationCount());
    return 0;
}
