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
    /* Pause-menu screen palette (the light theme): sampled from the
     * composed item-screen layers and the message palette. */
    SSC_MENU_CREAM,      /* backdrop parchment (BG3 flat color) */
    SSC_MENU_STONE,      /* slab plate stone */
    SSC_MENU_STONE_DARK, /* recessed well interior */
    SSC_MENU_INK,        /* menu ink (the dark text body color) */
    SSC_MENU_BLACK,      /* message palette black (outlines) */
    SSC_MENU_WHITE,      /* message palette white */
    SSC_MENU_RED,        /* menu red accent (red text body) */
    SSC_BANNER_NAVY,     /* the banner font's dark outline blue */
    SSC_COUNT
};

/* Text styles for the ROM message font — each is a (fill_type, charColor)
 * pair of the game's own text color tables. */
enum {
    SS_TEXT_INK = 0, /* dark ink on light plates (fill 7 / color 0) */
    SS_TEXT_WHITE,   /* white with silver shading, for dark chips (5 / 0) */
    SS_TEXT_RED,     /* menu red accent (5 / 1) */
    SS_TEXT_GREEN,   /* menu green (5 / 2) */
    SS_TEXT_NAVY,    /* banner-navy body — big font only (small font: ink) */
    SS_TEXT_STYLE_COUNT
};

/* Chip styles: the rounded message chips of border_type 9, in the game's
 * own fill schemes (DispMessageFrame family). */
enum {
    SS_CHIP_DARK = 0, /* black interior — the pause menu's name chips */
    SS_CHIP_RED,      /* brick-red interior — the header/selected chip */
    SS_CHIP_STYLE_COUNT
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

/* ------------------------------------------------------------------ *
 * Pause-menu dressing, decoded from the START menu's own screens      *
 * (item screen: gfx groups 86 + 90, palette groups 11/12/181/182 —    *
 * the exact recipe sub_080A4D34 + sub_080A4DB8 run). All built in the *
 * same lazy Ready() pass; every call degrades to palette-toned        *
 * procedural stand-ins until then.                                    *
 * ------------------------------------------------------------------ */

/* 1 once the pause-menu layers decoded (implies Ready()). The light
 * theme's fully-authentic path; callers may branch to simpler dressing
 * while 0 (the colors above still return usable neutral stand-ins). */
int Port_SecondScreenTheme_MenuReady(void);

/* Fills rect with the pause menu's parchment backdrop: flat cream plus
 * the Ezlo doodle pattern on its original diagonal lattice, anchored to
 * the surface origin so panels never shift phase. scale is the integer
 * art scale. */
void Port_SecondScreenTheme_DrawBackdrop(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                         int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t scale);

/* Draws the menu's carved stone slab (the item screen's big plate) over
 * the rect: nine-sliced from the composed screen so the triforce corners
 * and carved bands stay pixel-authentic; interiors tile. */
void Port_SecondScreenTheme_DrawPlate(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                      int32_t x, int32_t y, int32_t w, int32_t h, int32_t scale);

/* Draws one recessed stone well (the item screen's slot/tray art),
 * nine-sliced from the bottle tray. Used for cells and list rows. */
void Port_SecondScreenTheme_DrawWell(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                     int32_t x, int32_t y, int32_t w, int32_t h, int32_t scale);

/* Draws a rounded message chip (border_type 9) in the given SS_CHIP_*
 * fill scheme — the pause menu's name/header chips. */
void Port_SecondScreenTheme_DrawChip(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                     int32_t x, int32_t y, int32_t w, int32_t h, int32_t tileScale,
                                     int style);

/* Text in the game's message font (gUnk_08109248 bank 0, per-glyph widths
 * from the metrics rows), colored through the game's own text LUTs
 * (SS_TEXT_*). Returns the advance in pixels, or 0 when the font is not
 * decoded yet (callers keep a procedural fallback). y is the glyph-box
 * top; visible ink spans rows 1..15 of the 16 px box. */
int32_t Port_SecondScreenTheme_DrawText(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                        int32_t x, int32_t y, int32_t scale, int style, const char* str);

/* Pixel width DrawText would advance (0 when the font is not ready). */
int32_t Port_SecondScreenTheme_TextWidth(const char* str, int32_t scale);

/* Text in the game's STYLIZED banner font — the fat white-on-navy
 * lettering of the area-name banners ("South Hyrule Field"): glyph bank 8
 * of gUnk_08109248, two 8x16 cells per glyph, replayed exactly like
 * ShowTextBox's stylized path (sub_0805F9A0 -> sub_0805F25C banks>4 ->
 * sub_080026F2's transparent-merge column writer, adjacent glyphs
 * overlapping one outline column). Same SS_TEXT_* color schemes; the
 * banners' own scheme is SS_TEXT_WHITE-on-chip / SS_TEXT_INK on plates.
 * y is the glyph-box top (16 rows at `scale`); returns the advance, or 0
 * when the bank isn't decoded (callers keep their fallback). */
int32_t Port_SecondScreenTheme_DrawBigText(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                           int32_t x, int32_t y, int32_t scale, int style, const char* str);

/* Pixel width DrawBigText would advance (0 when the bank is not ready). */
int32_t Port_SecondScreenTheme_BigTextWidth(const char* str, int32_t scale);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SECOND_SCREEN_THEME_H */
