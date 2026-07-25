#ifndef PORT_SECOND_SCREEN_THEME_H
#define PORT_SECOND_SCREEN_THEME_H

/*
 * TMC UI furniture for the second screen, decoded from ROM at runtime —
 * the pause menu's message-window chrome, the HUD's hearts / rupee / key
 * icons and digit fonts, and the pause menu's equip cursor and A/B button
 * bubbles. Zero baked pixels: everything is rebuilt from the same ROM
 * tables the game itself loads (gGfxGroups / gPaletteGroups / the text
 * border data), via the Port_* accessors at the end of src/common.c.
 *
 * Threading/caching contract: everything is built lazily, ONCE, on the
 * first Port_SecondScreenTheme_Ready() call that finds the ROM tables
 * resolved, into private RGBA buffers that are immutable afterwards. Only
 * the second-screen render thread calls into this module, so there is no
 * cross-thread publication to manage — but the build still reads nothing
 * live: ROM-const data only (same policy as port_second_screen_render.c).
 * Per-frame cost after the build is scaled blits of the cached buffers.
 *
 * No Android headers — plain C over caller-provided RGBA8888 buffers, so
 * the file compiles (as dead code) on every platform this port targets.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One cached theme element: RGBA8888, row-major, w*h pixels. Alpha 0 =
 * transparent (GBA color index 0). Pointers stay valid for the process
 * lifetime once Ready() returns 1. */
typedef struct {
    const uint32_t* px;
    int32_t w, h;
} SecondScreenThemeSprite;

enum {
    /* HUD heart tiles (8x8): quarter-heart granularity, exactly the five
     * states DrawHearts (src/ui.c) can put in the tilemap. */
    SST_HEART_EMPTY,
    SST_HEART_Q1,
    SST_HEART_Q2,
    SST_HEART_Q3,
    SST_HEART_FULL,
    /* HUD rupee icon (16x16), one per wallet tier — the HUD swaps the icon
     * with the wallet (gWalletSizes[].iconStartTile). */
    SST_RUPEE_WALLET0,
    SST_RUPEE_WALLET1,
    SST_RUPEE_WALLET2,
    SST_RUPEE_WALLET3,
    /* HUD small-key icon (16x16, tiles 0x1C..0x1F per DrawKeys). */
    SST_KEY,
    /* HUD counter font (8x16): the digits RenderDigits stamps for the
     * rupee/key counters. White = normal, yellow = maxed-out counter. */
    SST_DIGIT_WHITE_0, /* ..._0 + digit, ten consecutive ids */
    SST_DIGIT_YELLOW_0 = SST_DIGIT_WHITE_0 + 10,
    /* HUD ammo-count font (8x8): the tiny bomb/arrow count under the
     * equipped item, tens glyph right-aligned + ones glyph left-aligned
     * (sub_0801C2F0 layout). */
    SST_SMALL_TENS_0 = SST_DIGIT_YELLOW_0 + 10,
    SST_SMALL_ONES_0 = SST_SMALL_TENS_0 + 10,
    /* HUD button bubbles (sprite 505 frames 0/1) — the A and B badges. */
    SST_BUTTON_A = SST_SMALL_ONES_0 + 10,
    SST_BUTTON_B,
    /* Pause-menu equip cursor (the gold slot frame), both blink frames. */
    SST_CURSOR_0,
    SST_CURSOR_1,
    SST_COUNT
};

/* Colors sampled/derived from the decoded art (not hardcoded), for the
 * procedural parts of the panel that need to sit in the same palette.
 * Valid (non-fallback) only once Ready() returns 1. */
enum {
    SSC_WINDOW_FILL,   /* message-window interior fill */
    SSC_BORDER_LIGHT,  /* brightest border color */
    SSC_BORDER_DARK,   /* darkest border color */
    SSC_GOLD,          /* key-icon gold (cursor/accent gold) */
    SSC_HEART_RED,     /* full-heart red */
    SSC_RUPEE_GREEN,   /* wallet-0 rupee green */
    SSC_TEXT_LIGHT,    /* HUD digit body color */
    SSC_COUNT
};

/* Attempts the one-time lazy build (cheap no-op once decided) and returns
 * 1 when the theme is available. Call only while the game is in gameplay
 * (snapshot.inGame) so the ROM tables are guaranteed resolved; before
 * that it returns 0 and the caller uses its neutral fallbacks. */
int Port_SecondScreenTheme_Ready(void);

/* Cached element by SST_* id, or NULL when that element failed to decode
 * (callers fall back per element). */
const SecondScreenThemeSprite* Port_SecondScreenTheme_Get(int id);

/* RGBA color by SSC_* id. Always returns a usable color: a neutral
 * stand-in before Ready(), the palette-derived value after. */
uint32_t Port_SecondScreenTheme_Color(int id);

/* Raw RGB555 colors (16 entries) of one OBJ palette bank as the pause
 * menu leaves it loaded — the bank state sprite OBJ pieces select with
 * their palette bits (group 182's banks 5..10 over 181/11's banks 0..4).
 * Shared with the item-icon renderer so both piece decoders resolve
 * banks identically. Independent of Ready(): NULL only while the ROM
 * palette tables are still unresolved. */
const uint16_t* Port_SecondScreenTheme_ObjPalette(uint32_t bank);

/* Draws a TMC message-style window (DispMessageFrame's exact tile
 * arrangement: corner/edge tiles around a solid fill) covering the given
 * rect, border tiles scaled by tileScale (integer, nearest-neighbor).
 * Falls back to a plain palette-toned frame when the border tiles didn't
 * decode. The rect is clipped to the buffer. */
void Port_SecondScreenTheme_DrawWindow(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                       int32_t x, int32_t y, int32_t w, int32_t h, int32_t tileScale);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SECOND_SCREEN_THEME_H */
