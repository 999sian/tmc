/*
 * port/rando/rando_cosmetic.cpp — eventdefine-driven cosmetic palette
 * overrides (MinishMaker parity: tunic color, heart fill/outline color,
 * rainbow hearts).
 *
 * MinishMaker patches palette data in ROM:
 *   tunic.event:  SHORTs at EU 0x5A2596/98/9A/9E = gPalette_14 (Link,
 *                 objPaletteId 0x16) entries 3 (DARKEST), 4 (DARK),
 *                 5 (MAIN), 7 (LIGHT).
 *   hearts.event: vanilla in-game heart tiles (4bpp, BG palette row 15 =
 *                 gPalette_12, HUD palette group 0xC) use entry 9 for the
 *                 red fill and entry 14 for the white outline (11 = empty
 *                 gray, 15 = black shadow — untouched).
 *
 * The port cannot patch source data in place (gGlobalGfxAndPalettes is a
 * const view into gRomData, and the extracted-assets path loads palettes
 * from per-file heap buffers instead). So this module is content-addressed:
 * once per frame it scans the 32 gPaletteBuffer rows for the *vanilla
 * fingerprint* of the two palettes and rewrites the target entries wherever
 * they appear. That transparently covers every load path (ROM blob, asset
 * JSON, LoadObjPaletteAtIndex into a dynamic OBJ slot, the file-select Link
 * preview row 31) and every reload (room change, menu exit), because a
 * reload restores the vanilla colors and the next tick re-patches them.
 * FadeVBlank() only uploads rows flagged in gUsedPalettes, so every write
 * also sets the row's bit; the tick runs from VBlankIntrWait() (i.e. before
 * WaitForNextFrame()'s FadeVBlank()), so a patch lands in PAL_RAM the same
 * frame the engine loads the palette — no vanilla flash.
 *
 * Fixed state only; no heap, no per-frame allocation. Inactive seed or
 * absent defines => the scan is skipped entirely.
 */

#include "rando/rando.h"
#include "rando/rando_logic.h"

#include <stdint.h>
#include <stdio.h>

extern "C" uint16_t gPaletteBuffer[]; /* u16[0x200] — 32 rows x 16 colors */
extern "C" uint32_t gUsedPalettes;    /* FadeVBlank upload mask, 1 bit/row */

/* ---- vanilla fingerprints (identical palette data on USA and EU) -------- */

/* gPalette_14 (Link) tunic greens. Indices we both match on and overwrite. */
#define LINK_IDX_DARKEST 3
#define LINK_IDX_DARK 4
#define LINK_IDX_MAIN 5
#define LINK_IDX_LIGHT 7
#define LINK_VAN_DARKEST 0x15E2u
#define LINK_VAN_DARK 0x1688u
#define LINK_VAN_MAIN 0x07E2u
#define LINK_VAN_LIGHT 0x53F7u

/* gPalette_12 (HUD, BG row 15). Detection uses entries this module never
 * writes (the wallet reds + the rupee yellow), so a row keeps matching while
 * the heart entries are being mutated (needed for rainbow). */
#define HUD_IDX_WALLET_RED1 7
#define HUD_IDX_WALLET_RED2 8
#define HUD_IDX_YELLOW 10
#define HUD_VAN_WALLET_RED1 0x0CAFu
#define HUD_VAN_WALLET_RED2 0x10D7u
#define HUD_VAN_YELLOW 0x03DFu
#define HUD_IDX_HEART_FILL 9  /* vanilla 0x195F */
#define HUD_IDX_HEART_EDGE 14 /* vanilla 0x7FFF — shared HUD white, see note */

/* Upstream hearts.event rainbow table: 16 colors, 12 frames each. */
static const uint16_t kRainbow[16] = {
    0x5749, 0x3F49, 0x3B49, 0x274A, 0x2753, 0x2759, 0x26BA, 0x261A,
    0x25DA, 0x257A, 0x253A, 0x3D3A, 0x613A, 0x61B6, 0x5E13, 0x5A90,
};
#define RAINBOW_FRAMES_PER_COLOR 12u

/* ---- cached per-seed configuration -------------------------------------- */

typedef struct {
    bool valid;       /* defines evaluated for `seed` */
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

static bool EvalColor555(const char* name, uint64_t seed, uint16_t* out) {
    uint32_t v = 0;
    if (!RandoLogic_EvalEventDefine(name, seed, &v)) {
        return false;
    }
    *out = (uint16_t)(v & 0x7FFF);
    return true;
}

/* heartFillR/G/B (and heartEdge*) are the 5-bit components of one RGB555
 * color (R = low bits, GBA layout). Reassemble (B<<10)|(G<<5)|R. */
static bool EvalColorRGB(const char* base_r, const char* base_g, const char* base_b,
                         uint64_t seed, uint16_t* out) {
    uint32_t r = 0, g = 0, b = 0;
    if (!RandoLogic_EvalEventDefine(base_r, seed, &r) ||
        !RandoLogic_EvalEventDefine(base_g, seed, &g) ||
        !RandoLogic_EvalEventDefine(base_b, seed, &b)) {
        return false;
    }
    *out = (uint16_t)(((b & 0x1F) << 10) | ((g & 0x1F) << 5) | (r & 0x1F));
    return true;
}

static void RefreshConfig(uint64_t seed) {
    bool has_value = false;

    sCfg.valid = true;
    sCfg.seed = seed;

    sCfg.tunic = EvalColor555("TUNIC_COLOR_MAIN", seed, &sCfg.tunic_main) &&
                 EvalColor555("TUNIC_COLOR_DARK", seed, &sCfg.tunic_dark) &&
                 EvalColor555("TUNIC_COLOR_DARKEST", seed, &sCfg.tunic_darkest) &&
                 EvalColor555("TUNIC_COLOR_LIGHT", seed, &sCfg.tunic_light);

    sCfg.rainbow = RandoLogic_HasEventDefine("heartscolorrainbow", &has_value);
    sCfg.heart_fill =
        !sCfg.rainbow && EvalColorRGB("heartFillR", "heartFillG", "heartFillB", seed,
                                      &sCfg.heart_fill_color);
    sCfg.heart_edge =
        EvalColorRGB("heartEdgeR", "heartEdgeG", "heartEdgeB", seed, &sCfg.heart_edge_color);

    sRainbowFrame = 0;

    if (sCfg.tunic || sCfg.heart_fill || sCfg.heart_edge || sCfg.rainbow) {
        fprintf(stderr,
                "[RANDO] cosmetic: tunic=%d (main=%04X dark=%04X darkest=%04X light=%04X) "
                "heartFill=%d (%04X) heartEdge=%d (%04X) rainbow=%d\n",
                sCfg.tunic, sCfg.tunic_main, sCfg.tunic_dark, sCfg.tunic_darkest,
                sCfg.tunic_light, sCfg.heart_fill, sCfg.heart_fill_color, sCfg.heart_edge,
                sCfg.heart_edge_color, sCfg.rainbow);
    }
}

/* Write `color` to row entry `idx` iff different; flags the row for the
 * FadeVBlank PAL_RAM upload only on an actual change. */
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

/* Re-evaluate the eventdefines and re-apply immediately (e.g. right after a
 * seed is committed or a rando save loads). The per-frame tick keeps things
 * correct on its own; this just removes the one-frame refresh latency. */
extern "C" void Rando_Cosmetic_Apply(void) {
    sCfg.valid = false;
    Rando_Cosmetic_Tick();
}
