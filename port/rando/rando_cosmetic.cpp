/*
 * port/rando/rando_cosmetic.cpp — native cosmetic palette overrides.
 */

#include "rando/rando.h"

#include <stdint.h>
#include <stdio.h>

extern "C" uint16_t gPaletteBuffer[]; /* u16[0x200] — 32 rows x 16 colors */
extern "C" uint32_t gUsedPalettes;    /* FadeVBlank upload mask, 1 bit/row */

/* ---- vanilla fingerprints (identical palette data on USA and EU) -------- */
#define LINK_IDX_DARKEST 3
#define LINK_IDX_DARK 4
#define LINK_IDX_MAIN 5
#define LINK_IDX_LIGHT 7
#define LINK_VAN_DARKEST 0x15E2u
#define LINK_VAN_DARK 0x1688u
#define LINK_VAN_MAIN 0x07E2u
#define LINK_VAN_LIGHT 0x53F7u

#define HUD_IDX_WALLET_RED1 7
#define HUD_IDX_WALLET_RED2 8
#define HUD_IDX_YELLOW 10
#define HUD_VAN_WALLET_RED1 0x0CAFu
#define HUD_VAN_WALLET_RED2 0x10D7u
#define HUD_VAN_YELLOW 0x03DFu
#define HUD_IDX_HEART_FILL 9
#define HUD_IDX_HEART_EDGE 14

static const uint16_t kRainbow[16] = {
    0x5749, 0x3F49, 0x3B49, 0x274A, 0x2753, 0x2759, 0x26BA, 0x261A,
    0x25DA, 0x257A, 0x253A, 0x3D3A, 0x613A, 0x61B6, 0x5E13, 0x5A90,
};
#define RAINBOW_FRAMES_PER_COLOR 12u

typedef struct {
    uint64_t state;
} SplitMix64;

static uint64_t SplitMix64_Next(SplitMix64* rng) {
    uint64_t z = (rng->state += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

static uint32_t RngBounded(SplitMix64* rng, uint32_t bound) {
    if (bound <= 1) return 0;
    return (uint32_t)(SplitMix64_Next(rng) % bound);
}

typedef struct {
    bool valid;
    uint64_t seed;
    bool tunic;
    uint16_t tunic_main, tunic_dark, tunic_darkest, tunic_light;
    bool heart_fill;
    uint16_t heart_fill_color;
    bool heart_edge;
    uint16_t heart_edge_color;
    bool rainbow;
} CosmeticConfig;

static CosmeticConfig sCfg;
static uint32_t sRainbowFrame;

static void RefreshConfig(uint64_t seed) {
    sCfg.valid = true;
    sCfg.seed = seed;
    sCfg.tunic = false;
    sCfg.heart_fill = false;
    sCfg.heart_edge = false;
    sCfg.rainbow = false;

    RandomizerSettings settings = Rando_GetSettings();

    // Map tunic color
    // 0 = Vanilla/Green, 1 = Red, 2 = Blue, 3 = Purple, 4 = Orange, 5 = Grey, 6 = Random
    int tunic_choice = settings.tunic_color;
    if (tunic_choice == 6) {
        SplitMix64 rng;
        rng.state = seed ^ 0xbadc0deull;
        tunic_choice = (int)(RngBounded(&rng, 6));
    }

    if (tunic_choice != 0) {
        sCfg.tunic = true;
        switch (tunic_choice) {
            case 1: // Red
                sCfg.tunic_main = 0x0014; sCfg.tunic_dark = 0x000F; sCfg.tunic_darkest = 0x000A; sCfg.tunic_light = 0x357A;
                break;
            case 2: // Blue
                sCfg.tunic_main = 0x7400; sCfg.tunic_dark = 0x5800; sCfg.tunic_darkest = 0x4000; sCfg.tunic_light = 0x7DF4;
                break;
            case 3: // Purple
                sCfg.tunic_main = 0x6412; sCfg.tunic_dark = 0x480C; sCfg.tunic_darkest = 0x3408; sCfg.tunic_light = 0x751C;
                break;
            case 4: // Orange
                sCfg.tunic_main = 0x027E; sCfg.tunic_dark = 0x01B9; sCfg.tunic_darkest = 0x0114; sCfg.tunic_light = 0x4A7F;
                break;
            case 5: // Grey
                sCfg.tunic_main = 0x4210; sCfg.tunic_dark = 0x318C; sCfg.tunic_darkest = 0x2108; sCfg.tunic_light = 0x5EF7;
                break;
        }
    }

    // Map heart color
    // 0 = Red/Vanilla, 1 = Blue, 2 = Green, 3 = Yellow, 4 = Purple, 5 = Rainbow, 6 = Random
    int heart_choice = settings.heart_color;
    if (heart_choice == 6) {
        SplitMix64 rng;
        rng.state = seed ^ 0xf00dcafeull;
        heart_choice = (int)(RngBounded(&rng, 6));
    }

    if (heart_choice == 5) {
        sCfg.rainbow = true;
    } else if (heart_choice != 0) {
        sCfg.heart_fill = true;
        sCfg.heart_edge = true;
        sCfg.heart_edge_color = 0x7FFF; // White outline
        switch (heart_choice) {
            case 1: // Blue
                sCfg.heart_fill_color = 0x7400;
                break;
            case 2: // Green
                sCfg.heart_fill_color = 0x03E0;
                break;
            case 3: // Yellow
                sCfg.heart_fill_color = 0x03DF;
                break;
            case 4: // Purple
                sCfg.heart_fill_color = 0x6412;
                break;
        }
    }
}

static inline void PutColor(uint16_t* row, uint32_t row_index, uint32_t idx, uint16_t color) {
    if (row[idx] != color) {
        row[idx] = color;
        gUsedPalettes |= 1u << row_index;
    }
}

extern "C" void Rando_Cosmetic_Tick(void) {
    if (!Rando_IsActive()) {
        if (sCfg.valid) {
            sCfg = CosmeticConfig{};
        }
        return;
    }

    const uint64_t seed = Rando_GetSeed64();
    if (!sCfg.valid || sCfg.seed != seed) {
        RefreshConfig(seed);
    }

    const bool hearts = sCfg.heart_fill || sCfg.heart_edge || sCfg.rainbow;
    if (!sCfg.tunic && !hearts) {
        return;
    }

    uint16_t rainbow_color = 0;
    if (sCfg.rainbow) {
        rainbow_color = kRainbow[(sRainbowFrame / RAINBOW_FRAMES_PER_COLOR) % 16u];
        sRainbowFrame++;
    }

    for (uint32_t r = 0; r < 32; ++r) {
        uint16_t* row = &gPaletteBuffer[r * 16];

        if (sCfg.tunic && row[LINK_IDX_MAIN] == LINK_VAN_MAIN &&
            row[LINK_IDX_DARK] == LINK_VAN_DARK && row[LINK_IDX_DARKEST] == LINK_VAN_DARKEST &&
            row[LINK_IDX_LIGHT] == LINK_VAN_LIGHT) {
            PutColor(row, r, LINK_IDX_MAIN, sCfg.tunic_main);
            PutColor(row, r, LINK_IDX_DARK, sCfg.tunic_dark);
            PutColor(row, r, LINK_IDX_DARKEST, sCfg.tunic_darkest);
            PutColor(row, r, LINK_IDX_LIGHT, sCfg.tunic_light);
            continue;
        }

        if (!hearts) {
            continue;
        }
        if (row[HUD_IDX_WALLET_RED1] == HUD_VAN_WALLET_RED1 &&
            row[HUD_IDX_WALLET_RED2] == HUD_VAN_WALLET_RED2 &&
            row[HUD_IDX_YELLOW] == HUD_VAN_YELLOW) {
            if (sCfg.heart_fill) {
                PutColor(row, r, HUD_IDX_HEART_FILL, sCfg.heart_fill_color);
            } else if (sCfg.rainbow) {
                PutColor(row, r, HUD_IDX_HEART_FILL, rainbow_color);
            }
            if (sCfg.heart_edge) {
                PutColor(row, r, HUD_IDX_HEART_EDGE, sCfg.heart_edge_color);
            }
        }
    }
}

extern "C" void Rando_Cosmetic_Apply(void) {
    sCfg.valid = false;
    Rando_Cosmetic_Tick();
}
