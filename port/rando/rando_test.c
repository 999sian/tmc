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
    if (n > cap) return 0;
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
                fprintf(stderr, "rando_test: determinism mismatch at %zu (%u vs %u)\n",
                        i, (unsigned)a[i], (unsigned)b[i]);
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

    fprintf(stderr, "ALL TESTS PASS\n");
    return 0;
}
