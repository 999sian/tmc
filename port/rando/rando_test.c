#include "rando/rando.h"
#include "rando/rando_newfile.h"
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
    if (n > cap)
        return 0;
    memcpy(out, table, n * sizeof(out[0]));
    return 1;
}

static int run_determinism_test(void) {
    uint16_t a[RANDO_LOCATION_COUNT];
    uint16_t b[RANDO_LOCATION_COUNT];
    RandomizerSettings settings = Rando_DefaultSettings();

    if (!GenerateSeed(0xfeedfaceu, settings) || !Rando_VerifyCurrentSeed() || !copy_table(a, RANDO_LOCATION_COUNT)) {
        fprintf(stderr, "rando_test: first generation failed\n");
        return 0;
    }
    if (!GenerateSeed(0xfeedfaceu, settings) || !Rando_VerifyCurrentSeed() || !copy_table(b, RANDO_LOCATION_COUNT)) {
        fprintf(stderr, "rando_test: second generation failed\n");
        return 0;
    }
    if (memcmp(a, b, sizeof(a)) != 0) {
        for (size_t i = 0; i < RANDO_LOCATION_COUNT; ++i) {
            if (a[i] != b[i]) {
                fprintf(stderr, "rando_test: determinism mismatch at %zu (%u vs %u)\n", i, (unsigned)a[i],
                        (unsigned)b[i]);
                break;
            }
        }
        return 0;
    }
    Rando_Reset();
    return 1;
}

static int run_override_test(void) {
    RandomizerSettings settings = Rando_DefaultSettings();
    if (!GenerateSeed(0xfeedfaceu, settings) || !Rando_IsActive()) {
        fprintf(stderr, "rando_test: generation failed\n");
        return 0;
    }

    // Key of Smith_Floor_Item1 is 0x002211E0u
    unsigned char type = 0x99;
    unsigned char sub = 0;
    if (!Rando_OverrideLocationKey(0x002211E0u, &type, &sub)) {
        fprintf(stderr, "rando_test: failed to override location key\n");
        Rando_Reset();
        return 0;
    }
    if (type == 0x99) {
        fprintf(stderr, "rando_test: override did not change type\n");
        Rando_Reset();
        return 0;
    }

    Rando_Reset();
    return 1;
}

/* The glitch-logic tier must (a) still produce beatable seeds and (b) be
 * deterministic for a given seed+tricks. Generates with the full trick set
 * enabled (glitchless off) and asserts both. */
static int run_glitched_logic_test(void) {
    uint16_t a[RANDO_LOCATION_COUNT];
    uint16_t b[RANDO_LOCATION_COUNT];
    RandomizerSettings settings = Rando_DefaultSettings();
    settings.glitchless_logic = false;
    settings.tricks = RANDO_TRICK_ALL;

    if (!GenerateSeed(0x12345678u, settings) || !Rando_VerifyCurrentSeed() || !copy_table(a, RANDO_LOCATION_COUNT)) {
        fprintf(stderr, "rando_test: glitched generation/verify failed (unbeatable?)\n");
        return 0;
    }
    if (!GenerateSeed(0x12345678u, settings) || !Rando_VerifyCurrentSeed() || !copy_table(b, RANDO_LOCATION_COUNT)) {
        fprintf(stderr, "rando_test: glitched second generation failed\n");
        return 0;
    }
    if (memcmp(a, b, sizeof(a)) != 0) {
        fprintf(stderr, "rando_test: glitched determinism mismatch\n");
        return 0;
    }
    Rando_Reset();
    return 1;
}

/* Distinct nonzero seed for loop index i; seed 0 would trigger auto-seed and
 * break the fixed-seed determinism these tests rely on. */
static uint64_t test_seed(unsigned i) {
    return 0x1000u + (uint64_t)(i + 1) * 0x2545F4914F6CDD1Dull;
}

/* Generate `count` distinct seeds under `access` (default otherwise) and count
 * how many succeed. Every seed that GenerateSeed accepts MUST re-verify true —
 * generation claims beatability, so a table it activates that the verifier
 * rejects is a broken invariant. Returns successes, or -1 on such a violation
 * (message already printed). */
static int access_success_count(RandoAccessibility access, unsigned count) {
    int successes = 0;
    for (unsigned i = 0; i < count; ++i) {
        uint64_t seed = test_seed(i);
        RandomizerSettings settings = Rando_DefaultSettings();
        settings.accessibility = access;
        if (GenerateSeed(seed, settings)) {
            if (!Rando_VerifyCurrentSeed()) {
                fprintf(stderr, "rando_test: seed 0x%llx generated under accessibility %d but re-verify FAILED\n",
                        (unsigned long long)seed, (int)access);
                Rando_Reset();
                return -1;
            }
            successes++;
        }
        Rando_Reset();
    }
    return successes;
}

/* Test 1: strengthening accessibility still yields beatable seeds. Every seed
 * that generates under a strong mode must re-verify true (checked inside
 * access_success_count), and a healthy majority of seeds must succeed — a
 * regression that makes a strong mode reject everything trips the floor. */
static int run_accessibility_beatable_test(void) {
    const unsigned kSeeds = 20;
    const int kFloor = 15;

    int all_locations = access_success_count(RANDO_ACCESS_ALL_LOCATIONS, kSeeds);
    if (all_locations < 0)
        return 0; /* violation already reported */
    if (all_locations < kFloor) {
        fprintf(stderr, "rando_test: ALL_LOCATIONS only %d/%u seeds beatable (floor %d) — regression?\n", all_locations,
                kSeeds, kFloor);
        return 0;
    }

    int all_nonkeys = access_success_count(RANDO_ACCESS_ALL_NONKEYS, kSeeds);
    if (all_nonkeys < 0)
        return 0;
    if (all_nonkeys < kFloor) {
        fprintf(stderr, "rando_test: ALL_NONKEYS only %d/%u seeds beatable (floor %d) — regression?\n", all_nonkeys,
                kSeeds, kFloor);
        return 0;
    }
    return 1;
}

/* Test 2: strengthening only. A mode above GOAL can reject a seed GOAL accepts,
 * never the reverse — accessibility never perturbs placement, only the accept
 * test. So any seed that generates under ALL_LOCATIONS must also generate under
 * GOAL. Loops several seeds and checks every ALL_LOCATIONS success. */
static int run_accessibility_monotonic_test(void) {
    const unsigned kSeeds = 20;
    int checked = 0;
    for (unsigned i = 0; i < kSeeds; ++i) {
        uint64_t seed = test_seed(i);
        RandomizerSettings strong = Rando_DefaultSettings();
        strong.accessibility = RANDO_ACCESS_ALL_LOCATIONS;
        bool strong_ok = GenerateSeed(seed, strong);
        Rando_Reset();
        if (!strong_ok)
            continue;

        RandomizerSettings goal = Rando_DefaultSettings();
        goal.accessibility = RANDO_ACCESS_GOAL;
        bool goal_ok = GenerateSeed(seed, goal);
        Rando_Reset();
        if (!goal_ok) {
            fprintf(stderr,
                    "rando_test: seed 0x%llx generated under ALL_LOCATIONS but FAILED under GOAL "
                    "(strengthening violated)\n",
                    (unsigned long long)seed);
            return 0;
        }
        checked++;
    }
    if (checked == 0) {
        fprintf(stderr, "rando_test: monotonic test found no ALL_LOCATIONS successes to compare\n");
        return 0;
    }
    return 1;
}

/* Test 3: all-locations really means reachable. After a successful
 * ALL_LOCATIONS generation the verifier — the oracle — must accept the active
 * table, proving generation only activates tables it will re-verify. Finds a
 * succeeding seed dynamically rather than hardcoding one. */
static int run_accessibility_reachable_test(void) {
    for (unsigned i = 0; i < 64; ++i) {
        uint64_t seed = test_seed(i);
        RandomizerSettings settings = Rando_DefaultSettings();
        settings.accessibility = RANDO_ACCESS_ALL_LOCATIONS;
        if (GenerateSeed(seed, settings)) {
            bool ok = Rando_VerifyCurrentSeed();
            Rando_Reset();
            if (!ok) {
                fprintf(stderr, "rando_test: ALL_LOCATIONS seed 0x%llx active but verifier rejected it\n",
                        (unsigned long long)seed);
                return 0;
            }
            return 1;
        }
        Rando_Reset();
    }
    fprintf(stderr, "rando_test: no ALL_LOCATIONS seed generated in 64 tries — cannot test reachability\n");
    return 0;
}

/* Test 4: fingerprint is stable for identical settings, sensitive to every
 * placement-affecting setting, blind to cosmetic/runtime-only fields, and masks
 * trick bits when glitchless logic is on. */
static int run_fingerprint_test(void) {
    RandomizerSettings base = Rando_DefaultSettings();

    /* (a) identical settings → identical fingerprint. */
    uint32_t fp = Rando_SettingsFingerprint(&base);
    if (Rando_SettingsFingerprint(&base) != fp) {
        fprintf(stderr, "rando_test: fingerprint unstable for identical settings\n");
        return 0;
    }

    /* (b) placement-affecting changes must move the fingerprint. */
    RandomizerSettings dojos = base;
    dojos.shuffle_dojos = !base.shuffle_dojos;
    if (Rando_SettingsFingerprint(&dojos) == fp) {
        fprintf(stderr, "rando_test: toggling shuffle_dojos did not change fingerprint\n");
        return 0;
    }
    RandomizerSettings difficulty = base;
    difficulty.item_difficulty =
        (base.item_difficulty == RANDO_ITEM_POOL_NORMAL) ? RANDO_ITEM_POOL_HARD : RANDO_ITEM_POOL_NORMAL;
    if (Rando_SettingsFingerprint(&difficulty) == fp) {
        fprintf(stderr, "rando_test: changing item_difficulty did not change fingerprint\n");
        return 0;
    }
    RandomizerSettings access = base; /* base is RANDO_ACCESS_GOAL */
    access.accessibility = RANDO_ACCESS_ALL_LOCATIONS;
    if (Rando_SettingsFingerprint(&access) == fp) {
        fprintf(stderr, "rando_test: changing accessibility did not change fingerprint\n");
        return 0;
    }
    RandomizerSettings dungeon_items = base; /* default false → flip to true */
    dungeon_items.shuffle_dungeon_items = !base.shuffle_dungeon_items;
    if (Rando_SettingsFingerprint(&dungeon_items) == fp) {
        fprintf(stderr, "rando_test: toggling shuffle_dungeon_items did not change fingerprint\n");
        return 0;
    }

    /* (c) cosmetic / runtime-only fields the fingerprint excludes must NOT
     *     move it, even all toggled at once. */
    RandomizerSettings cosmetic = base;
    cosmetic.tunic_color = base.tunic_color + 3;
    cosmetic.heart_color = base.heart_color + 5;
    cosmetic.homewarp = !base.homewarp;
    cosmetic.instant_text = !base.instant_text;
    cosmetic.early_crests = !base.early_crests;
    if (Rando_SettingsFingerprint(&cosmetic) != fp) {
        fprintf(stderr, "rando_test: cosmetic/runtime-only field changed fingerprint (must be excluded)\n");
        return 0;
    }

    /* (d) tricks are masked under glitchless: two settings differing ONLY in
     *     .tricks fingerprint the same when glitchless is on, and differently
     *     when glitchless is off. */
    RandomizerSettings gl_a = base;
    gl_a.glitchless_logic = true;
    gl_a.tricks = 0u;
    RandomizerSettings gl_b = gl_a;
    gl_b.tricks = RANDO_TRICK_ALL;
    if (Rando_SettingsFingerprint(&gl_a) != Rando_SettingsFingerprint(&gl_b)) {
        fprintf(stderr, "rando_test: tricks changed fingerprint under glitchless (should be masked)\n");
        return 0;
    }
    RandomizerSettings ng_a = base;
    ng_a.glitchless_logic = false;
    ng_a.tricks = 0u;
    RandomizerSettings ng_b = ng_a;
    ng_b.tricks = RANDO_TRICK_ALL;
    if (Rando_SettingsFingerprint(&ng_a) == Rando_SettingsFingerprint(&ng_b)) {
        fprintf(stderr, "rando_test: tricks did not change fingerprint with glitchless off\n");
        return 0;
    }
    return 1;
}

/* Test 5: same seed + same ALL_LOCATIONS settings generates a byte-identical
 * table twice. Finds a succeeding seed dynamically, then compares two
 * independent generations of it. */
static int run_accessibility_determinism_test(void) {
    uint16_t a[RANDO_LOCATION_COUNT];
    uint16_t b[RANDO_LOCATION_COUNT];
    RandomizerSettings settings = Rando_DefaultSettings();
    settings.accessibility = RANDO_ACCESS_ALL_LOCATIONS;

    uint64_t seed = 0;
    bool found = false;
    for (unsigned i = 0; i < 64 && !found; ++i) {
        uint64_t candidate = test_seed(i);
        if (GenerateSeed(candidate, settings) && copy_table(a, RANDO_LOCATION_COUNT)) {
            seed = candidate;
            found = true;
        }
        Rando_Reset();
    }
    if (!found) {
        fprintf(stderr, "rando_test: no ALL_LOCATIONS seed generated in 64 tries — cannot test determinism\n");
        return 0;
    }

    if (!GenerateSeed(seed, settings) || !copy_table(b, RANDO_LOCATION_COUNT)) {
        fprintf(stderr, "rando_test: ALL_LOCATIONS re-generation of seed 0x%llx failed\n", (unsigned long long)seed);
        return 0;
    }
    if (memcmp(a, b, sizeof(a)) != 0) {
        for (size_t i = 0; i < RANDO_LOCATION_COUNT; ++i) {
            if (a[i] != b[i]) {
                fprintf(stderr, "rando_test: ALL_LOCATIONS determinism mismatch for seed 0x%llx at %zu (%u vs %u)\n",
                        (unsigned long long)seed, i, (unsigned)a[i], (unsigned)b[i]);
                break;
            }
        }
        Rando_Reset();
        return 0;
    }
    Rando_Reset();
    return 1;
}

int main(void) {
    fprintf(stderr, "Running native randomizer tests...\n");

    if (!run_determinism_test()) {
        fprintf(stderr, "FAIL: determinism test\n");
        return 1;
    }
    if (!run_override_test()) {
        fprintf(stderr, "FAIL: override test\n");
        return 1;
    }
    if (!run_glitched_logic_test()) {
        fprintf(stderr, "FAIL: glitched-logic test\n");
        return 1;
    }
    if (!run_accessibility_beatable_test()) {
        fprintf(stderr, "FAIL: accessibility-beatable test\n");
        return 1;
    }
    if (!run_accessibility_monotonic_test()) {
        fprintf(stderr, "FAIL: accessibility-monotonic (strengthening) test\n");
        return 1;
    }
    if (!run_accessibility_reachable_test()) {
        fprintf(stderr, "FAIL: accessibility-reachable test\n");
        return 1;
    }
    if (!run_fingerprint_test()) {
        fprintf(stderr, "FAIL: fingerprint stability/sensitivity test\n");
        return 1;
    }
    if (!run_accessibility_determinism_test()) {
        fprintf(stderr, "FAIL: accessibility-determinism test\n");
        return 1;
    }

    fprintf(stderr, "ALL TESTS PASS\n");
    return 0;
}
