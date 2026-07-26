/*
 * Second-screen compositor (AYN Thor bottom panel).
 *
 * Layout is a port of the sibling zelda3-android mod's bottom screen — the
 * design the project treats as reference — dressed in TMC's own art:
 *
 *   - MAP area top-left, full bleed (no inner box): outdoors the decoded
 *     Hyrule map with a gliding follow-cam (tap toggles the whole-map
 *     view; from there a tap on a map tile brackets it and zooms into the
 *     game's own enlarged regional map, windcrest warps draw as pins); in
 *     dungeons the authentic floor map with tappable floor plaques
 *     (preview auto-returns to Link's floor) and a name banner.
 *   - SIDEBAR right: hearts on top, the game's contextual R prompt under
 *     them (the cue a hidden top HUD would otherwise cost), the A/B equip
 *     rings in the middle (tap a ring to arm it, then tap an item to
 *     assign that slot), the rupee/keys chip at the bottom.
 *   - TAB BAR bottom: [QUEST][MAP][ITEMS] plus a square settings button,
 *     all in the pause menu's own button plate and all ending well above
 *     the Android gesture zone.
 *   - ITEMS / QUEST / SETTINGS tabs replace the map area with menu-style
 *     panels: the 4x4 equip grid, the quest-status screen, and the
 *     second screen's own toggles (persisted via port_runtime_config).
 *
 * Every metric scales with u = min(w,h)/720 — the same rule (and largely
 * the same constants) as the reference implementation, so the layout
 * survives both the wide dev surface and the Thor's near-square panel.
 *
 * The compositor core (Port_SecondScreen_PaintInto + Port_SecondScreen_OnTap
 * + all UI state) is platform-agnostic C compiled everywhere, so a host
 * harness can render and drive the exact layout the device shows; only the
 * ANativeWindow plumbing and the 20 Hz render thread are Android-only.
 * All art comes from the runtime-decoded theme/render/worldmap/dungeonmap
 * modules — no baked game pixels; every decoded element has a neutral
 * procedural fallback so the panel renders before the ROM is parsed.
 *
 * In-game dressing is the pause menu's own: the Ezlo-doodle parchment
 * backdrop, the carved stone slab as panel plate, its recessed wells as
 * cells/rows, the message chips for headers, and the game's message font
 * for every label (dark ink on the light plates, white on chips — the
 * menu's exact text schemes). Only the cinema screen stays dark.
 */

#include "port_second_screen.h"
#include "port_second_screen_dungeonmap.h"
#include "port_second_screen_quest.h"
#include "port_second_screen_render.h"
#include "port_second_screen_state.h"
#include "port_second_screen_theme.h"
#include "port_second_screen_worldmap.h"

#include "port_runtime_config.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Buffer pixel layout is RGBA_8888: as a little-endian u32 that's
 * A<<24 | B<<16 | G<<8 | R (same convention Rgb555ToRgba8888 in
 * port_second_screen_render.c emits). Build every color through this so
 * channel order mistakes can't creep in per call site. */
#define RGB(r, g, b) (0xFF000000u | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r))

/* In-game the panel dresses as a screen OF the pause menu: parchment
 * backdrop, stone slab plates, message chips and the message font, all
 * decoded from ROM by port_second_screen_theme.c. The only colors of our
 * own are the cinema screen's (kept deliberately dark and quiet) and the
 * four-element hues. */
#define COL_IDLE_BG RGB(0x05, 0x06, 0x05) /* idle: near-black */
#define COL_ELEM_EARTH RGB(0x58, 0xA0, 0x48)
#define COL_ELEM_FIRE RGB(0xD8, 0x58, 0x28)
#define COL_ELEM_WATER RGB(0x40, 0x70, 0xD0)
#define COL_ELEM_WIND RGB(0xC8, 0xD0, 0xD8)

/* Item ids used for icon/ammo special cases — transcribed from
 * include/item_ids.h (kept local so this file stays engine-header-free). */
#define ITEMID_BOMBS 0x07
#define ITEMID_REMOTE_BOMBS 0x08
#define ITEMID_BOW 0x09
#define ITEMID_LIGHT_ARROW 0x0A
#define ITEMID_BOTTLE1 0x1C
#define ITEMID_DUNGEON_MAP 0x50
#define ITEMID_COMPASS 0x51
#define ITEMID_BIG_KEY 0x52
#define ITEMID_KINSTONE 0x5C
/* Quest-screen ids, same transcription. */
#define ITEMID_SMITH_SWORD 0x01
#define ITEMID_QST_TINGLE_TROPHY 0x3D
#define ITEMID_QST_CARLOV_MEDAL 0x3E
#define ITEMID_SHELLS 0x3F
#define ITEMID_EARTH_ELEMENT 0x40 /* +1 fire, +2 water, +3 wind */
#define ITEMID_GRIP_RING 0x44     /* +1 bracelets, +2 flippers */
#define ITEMID_KINSTONE_BAG 0x67

/* Dungeon names + top-floor numbers for the plaque labels, transcribed
 * from gDungeonLayouts / gDungeonFloorMetadatas (src/common.c): display
 * row i (topmost first) is floor highest-2-i, positive = "<n>F", else
 * "B<1-n>" — e.g. Deepwood {3 floors, highest 3} = 1F, B1, B2. */
static const char* const kDungeonNames[7] = {
    NULL,
    "Deepwood Shrine",
    "Cave of Flames",
    "Fortress of Winds",
    "Temple of Droplets",
    "Palace of Winds",
    "Dark Hyrule Castle",
};
static const int8_t kDungeonTopFloor[7] = { 2, 3, 3, 5, 2, 7, 5 };

/* ------------------------------------------------------------------ */
/*  UI state shared with the tap handler                               */
/* ------------------------------------------------------------------ */

enum { SS_TAB_MAP = 0, SS_TAB_ITEMS, SS_TAB_QUEST, SS_TAB_SETTINGS };

/* What a tap target does when hit. arg meaning per action: item id,
 * tab id, ring (1 = A, 2 = B), plaque display-floor index, settings row.
 * SS_ACT_MAPVIEW is the map's own back affordance (region view -> whole
 * map, whole map -> follow cam) and carries no argument. */
enum {
    SS_ACT_ITEM = 1,
    SS_ACT_TAB,
    SS_ACT_RING,
    SS_ACT_PLAQUE,
    SS_ACT_SETTING,
    SS_ACT_MAP,
    SS_ACT_MAPVIEW,
};

/* Settings rows, top to bottom. The second-screen-only toggles persist
 * through their Port_Config accessors alone; the port-wide rows reuse the
 * exact live-apply calls the imgui F8 menu makes (port_imgui_menu.cpp) so
 * there is a single mechanism per setting, not parallel state. */
enum {
    SS_SET_TOP_HUD = 0,   /* hide_top_hud (engine gate ships separately) */
    SS_SET_FOLLOW,
    SS_SET_CRESTS,
    SS_SET_FLOOR_RETURN,
    SS_SET_VOLUME,        /* master_volume, cycles 0/25/50/75/100% */
    SS_SET_AUTOSAVE,      /* autosave_enabled via Port_QuickSave */
    SS_SET_COLOR_CORRECTION, /* color_correction + live PPU toggle */
    SS_SET_SHOW_FPS,      /* show_fps (overlay reads it per frame) */
    SS_SET_HOLD_ADVANCE,  /* hold_advance_text (message.c reads per frame) */
    SS_SET_COUNT
};

/* Port-wide live-apply entry points, declared like port_imgui_menu.cpp
 * declares them (this file stays engine-header-free): the audio mixer's
 * live volume, the quick-save autosaver's live toggle, and the PPU's live
 * color-correction switch. All extern "C" on their defining side. */
extern void Port_Audio_SetMasterVolume(float volume);
extern float Port_Audio_GetMasterVolume(void);
extern int Port_QuickSave_AutoEnabled(void);
extern void Port_QuickSave_SetAutoEnabled(int enabled);
extern void Port_Config_SetAutosaveEnabled(bool enabled);
extern void Port_PPU_SetColorCorrection(bool enabled);

#define SS_MAX_TARGETS 48
#define SS_NO_FLOOR (-128)
#define SS_FLOOR_RETURN_TICKS 120 /* 6 s at the 20 Hz paint rate */

/* Map-tile zoom states. The bracket beat exists so the zoom reads without
 * a cursor: the tapped tile is outlined on the whole map for a moment
 * (0.35 s at the 20 Hz paint rate) before the regional map replaces it. */
enum { SS_REGION_OFF = 0, SS_REGION_BRACKET, SS_REGION_VIEW };
#define SS_REGION_BRACKET_TICKS 7

typedef struct {
    int32_t x0, y0, x1, y1;
    uint8_t action;
    uint8_t arg;
} TapTarget;

typedef struct {
    TapTarget t[SS_MAX_TARGETS];
    int n;
} TargetList;

/* Tap targets + interactive UI state of the most recently painted frame.
 * The layout moves with surface size, so hit boxes are captured at paint
 * time and consumed by Port_SecondScreen_OnTap on the JNI thread; the same
 * lock guards the small state that thread mutates (active tab, map view,
 * region zoom...). Own mutex on Android (not the window mutex): taps must
 * never wait out a whole frame paint. Off Android everything runs on one thread
 * (the host harness), so the lock compiles away. */
static TapTarget sTapTargets[SS_MAX_TARGETS];
static int sTapTargetCount = 0;

static struct {
    uint8_t tab;
    uint8_t wholeMap;   /* map tab: whole-Hyrule view instead of follow cam */
    uint8_t armedRing;  /* 0 none, 1 = next item tap assigns A, 2 = B */
    int8_t floorPreview; /* plaque-selected display floor, SS_NO_FLOOR = live */
    uint32_t floorPreviewTick;
    int8_t previewBaseFloor; /* Link's floor when the preview was taken */
    uint8_t previewDungeon;
    /* Stamped by paint for the tap handler's decisions. */
    int8_t playerFloorDisp;
    uint8_t curDungeon;
    uint32_t lastTick;
    /* Overworld map transform of the last painted frame, so a tap can be
     * turned back into map-image coordinates (region picking). */
    uint8_t mapLive;
    float mapOx, mapOy, mapScale, mapU;
    int32_t mapImgW, mapImgH;
    /* Map-tile zoom: SS_REGION_* state, the picked tile's region id, and
     * its rect in world-map image pixels (what the bracket outlines). */
    uint8_t regionState;
    int32_t regionId;
    int32_t regionX0, regionY0, regionX1, regionY1;
    uint32_t regionTick;
    /* Set by paint when the zoom grid answers, so the tap handler knows
     * whether a map tap can zoom at all (stub -> plain follow/whole
     * toggle, exactly the behavior before the zoom existed). */
    uint8_t regionGridReady;
} sUi = { .floorPreview = SS_NO_FLOOR, .playerFloorDisp = SS_NO_FLOOR };

#ifdef __ANDROID__
#include <pthread.h>
static pthread_mutex_t sTapTargetMutex = PTHREAD_MUTEX_INITIALIZER;
#define UI_LOCK() pthread_mutex_lock(&sTapTargetMutex)
#define UI_UNLOCK() pthread_mutex_unlock(&sTapTargetMutex)
#else
#define UI_LOCK() ((void)0)
#define UI_UNLOCK() ((void)0)
#endif

/* Last successful world-map position fix. While Link is indoors (houses,
 * caves — anywhere LocatePlayer has no answer) the map keeps showing this
 * frozen fix, zelda3-android's doorway-marker behavior. Paint-thread
 * private; cleared when gameplay ends. */
static struct {
    int valid;
    int32_t mapX, mapY;
} sLastFix = { 0, 0, 0 };

/* Follow-cam state: view center (map-image coords) and zoom glide toward
 * their targets each frame, which animates both the follow pan and the
 * follow<->whole toggle. Paint-thread private. */
static struct {
    int valid;
    float x, y, scale;
} sCam = { 0, 0, 0, 0 };

/* ------------------------------------------------------------------ */
/*  Surface + primitives                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t* px;
    int32_t w, h, stride;
} SSurf;

static void FillRect(const SSurf* s, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    for (int32_t y = y0; y < y1; y++) {
        uint32_t* row = s->px + (size_t)y * (size_t)s->stride;
        for (int32_t x = x0; x < x1; x++) {
            row[x] = color;
        }
    }
}

static void OutlineRect(const SSurf* s, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t thickness,
                        uint32_t color) {
    FillRect(s, x0, y0, x1, y0 + thickness, color);
    FillRect(s, x0, y1 - thickness, x1, y1, color);
    FillRect(s, x0, y0, x0 + thickness, y1, color);
    FillRect(s, x1 - thickness, y0, x1, y1, color);
}

/* Channel-wise mix of two panel colors, t/256 toward b — used to derive
 * plate/well tones from the decoded window palette instead of inventing
 * new hues. */
static uint32_t MixColor(uint32_t a, uint32_t b, uint32_t t) {
    uint32_t r = ((a & 0xFF) * (256 - t) + (b & 0xFF) * t) >> 8;
    uint32_t g = (((a >> 8) & 0xFF) * (256 - t) + ((b >> 8) & 0xFF) * t) >> 8;
    uint32_t bl = (((a >> 16) & 0xFF) * (256 - t) + ((b >> 16) & 0xFF) * t) >> 8;
    return RGB(r, g, bl);
}

/* Nearest-neighbor integer-scale blit of a cached theme sprite; alpha-0
 * source pixels are skipped so sprites sit on whatever is behind them. */
static void BlitSprite(const SSurf* s, const SecondScreenThemeSprite* spr, int32_t x, int32_t y,
                       int32_t scale) {
    if (spr == NULL || spr->px == NULL || scale < 1) {
        return;
    }
    for (int32_t sy = 0; sy < spr->h; sy++) {
        for (int32_t sx = 0; sx < spr->w; sx++) {
            uint32_t c = spr->px[sy * spr->w + sx];
            if (c == 0) {
                continue;
            }
            int32_t dy0 = y + sy * scale;
            for (int32_t ey = 0; ey < scale; ey++) {
                int32_t dy = dy0 + ey;
                if (dy < 0 || dy >= s->h) {
                    continue;
                }
                uint32_t* row = s->px + (size_t)dy * (size_t)s->stride;
                int32_t dx0 = x + sx * scale;
                for (int32_t ex = 0; ex < scale; ex++) {
                    int32_t dx = dx0 + ex;
                    if (dx >= 0 && dx < s->w) {
                        row[dx] = c;
                    }
                }
            }
        }
    }
}

/* Chunky filled diamond (the four-element motif); r is the half-height. */
static void FillDiamond(const SSurf* s, int32_t cx, int32_t cy, int32_t r, uint32_t color) {
    for (int32_t i = -r; i <= r; i++) {
        int32_t half = r - (i < 0 ? -i : i);
        FillRect(s, cx - half, cy + i, cx + half + 1, cy + i + 1, color);
    }
}

/* Annulus between rIn..rOut — the equip rings and their pulse halo. */
static void FillRing(const SSurf* s, int32_t cx, int32_t cy, int32_t rOut, int32_t rIn, uint32_t color) {
    int32_t rO2 = rOut * rOut, rI2 = rIn * rIn;
    for (int32_t dy = -rOut; dy <= rOut; dy++) {
        int32_t y = cy + dy;
        if (y < 0 || y >= s->h) continue;
        uint32_t* row = s->px + (size_t)y * (size_t)s->stride;
        for (int32_t dx = -rOut; dx <= rOut; dx++) {
            int32_t x = cx + dx;
            if (x < 0 || x >= s->w) continue;
            int32_t d2 = dx * dx + dy * dy;
            if (d2 <= rO2 && d2 >= rI2) {
                row[x] = color;
            }
        }
    }
}

static void FillCircle(const SSurf* s, int32_t cx, int32_t cy, int32_t r, uint32_t color) {
    FillRing(s, cx, cy, r, 0, color);
}

/* Horizontal inset of a rounded rect's edge at scanline center `yc`. */
static float RoundRectInset(float y0, float y1, float r, float yc) {
    float dy = 0;
    if (yc < y0 + r) {
        dy = y0 + r - yc;
    } else if (yc > y1 - r) {
        dy = yc - (y1 - r);
    }
    if (dy <= 0) {
        return 0;
    }
    float t2 = r * r - dy * dy;
    return r - (t2 > 0 ? sqrtf(t2) : 0);
}

static float ClampRadius(float x0, float y0, float x1, float y1, float r) {
    if (r < 0) r = 0;
    if (r > (y1 - y0) / 2) r = (y1 - y0) / 2;
    if (r > (x1 - x0) / 2) r = (x1 - x0) / 2;
    return r;
}

/* Filled rounded rectangle (quarter-circle corners) — the base stroke of
 * the menu button plate; nesting insets gives keyline / keyline / fill
 * like the game's own SLEEP/SAVE buttons. */
static void FillRoundRect(const SSurf* s, float x0, float y0, float x1, float y1, float r,
                          uint32_t color) {
    r = ClampRadius(x0, y0, x1, y1, r);
    for (int32_t y = (int32_t)y0; y < (int32_t)y1; y++) {
        float dx = RoundRectInset(y0, y1, r, y + 0.5f);
        FillRect(s, (int32_t)(x0 + dx), y, (int32_t)(x1 - dx + 0.5f), y + 1, color);
    }
}

/* Rounded-rect keyline of thickness t, drawn without touching what it
 * encloses — so it can be laid over already-composed art (the active
 * tab's gold line over the button plate, the map's tile bracket). */
static void StrokeRoundRect(const SSurf* s, float x0, float y0, float x1, float y1, float r, float t,
                            uint32_t color) {
    if (t <= 0 || x1 - x0 <= 2 * t || y1 - y0 <= 2 * t) {
        return;
    }
    float ro = ClampRadius(x0, y0, x1, y1, r);
    float ri = ClampRadius(x0 + t, y0 + t, x1 - t, y1 - t, r - t);
    for (int32_t y = (int32_t)y0; y < (int32_t)y1; y++) {
        float yc = y + 0.5f;
        float dxo = RoundRectInset(y0, y1, ro, yc);
        if (yc < y0 + t || yc > y1 - t) {
            FillRect(s, (int32_t)(x0 + dxo), y, (int32_t)(x1 - dxo + 0.5f), y + 1, color);
            continue;
        }
        float dxi = t + RoundRectInset(y0 + t, y1 - t, ri, yc);
        FillRect(s, (int32_t)(x0 + dxo), y, (int32_t)(x0 + dxi + 0.5f), y + 1, color);
        FillRect(s, (int32_t)(x1 - dxi), y, (int32_t)(x1 - dxo + 0.5f), y + 1, color);
    }
}

/* Draws a '#' grid as square pixels of side `cell`, top-left at (x, y). */
static void DrawPixelArt(const SSurf* s, const char* const* rows, int nRows, float x, float y, float cell,
                         uint32_t color) {
    for (int ry = 0; ry < nRows; ry++) {
        const char* row = rows[ry];
        for (int rx = 0; row[rx] != '\0'; rx++) {
            if (row[rx] == '#') {
                FillRect(s, (int32_t)(x + rx * cell), (int32_t)(y + ry * cell),
                         (int32_t)(x + (rx + 1) * cell), (int32_t)(y + (ry + 1) * cell), color);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Label font                                                         */
/* ------------------------------------------------------------------ */

/* Labels render in the game's own message font (decoded from ROM by the
 * theme module). This original 5x7 caps face exists only as the pre-ROM
 * stand-in: the first frames before Port_LoadRom finishes, when no glyph
 * data exists to decode yet. One byte per row, bit 4 = leftmost column. */
static const uint8_t kFont5x7[][7] = {
    { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E }, /* 0 */
    { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E }, /* 1 */
    { 0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F }, /* 2 */
    { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E }, /* 3 */
    { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }, /* 4 */
    { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E }, /* 5 */
    { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E }, /* 6 */
    { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 }, /* 7 */
    { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }, /* 8 */
    { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C }, /* 9 */
    { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, /* A */
    { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E }, /* B */
    { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E }, /* C */
    { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E }, /* D */
    { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F }, /* E */
    { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 }, /* F */
    { 0x0E, 0x11, 0x10, 0x13, 0x11, 0x11, 0x0F }, /* G */
    { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, /* H */
    { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E }, /* I */
    { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C }, /* J */
    { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 }, /* K */
    { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F }, /* L */
    { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 }, /* M */
    { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 }, /* N */
    { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, /* O */
    { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 }, /* P */
    { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D }, /* Q */
    { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 }, /* R */
    { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E }, /* S */
    { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 }, /* T */
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, /* U */
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 }, /* V */
    { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 }, /* W */
    { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 }, /* X */
    { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 }, /* Y */
    { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F }, /* Z */
    { 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00 }, /* - */
    { 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10 }, /* / */
};

static int GlyphIndex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'Z') return 10 + (c - 'A');
    if (c >= 'a' && c <= 'z') return 10 + (c - 'a'); /* fold: no lowercase face */
    if (c == '-') return 36;
    if (c == '/') return 37;
    return -1; /* space and anything unknown: advance only */
}

static void DrawChar(const SSurf* s, int32_t x, int32_t y, int32_t scale, char c, uint32_t color) {
    int gi = GlyphIndex(c);
    if (gi < 0) return;
    for (int32_t py = 0; py < 7; py++) {
        uint8_t bits = kFont5x7[gi][py];
        for (int32_t px = 0; px < 5; px++) {
            if (bits & (0x10u >> px)) {
                FillRect(s, x + px * scale, y + py * scale, x + (px + 1) * scale, y + (py + 1) * scale,
                         color);
            }
        }
    }
}

/* Text with the 1px-scaled dark outline light glyphs need to read on any
 * backdrop (8-neighbor dark copies underneath, then the fill on top).
 * Returns the advance. */
static int32_t DrawTextStr(const SSurf* s, const char* str, int32_t x, int32_t y, int32_t scale,
                           uint32_t color, uint32_t outline) {
    int32_t cx = x;
    for (const char* p = str; *p; p++) {
        for (int32_t oy = -1; oy <= 1; oy++) {
            for (int32_t ox = -1; ox <= 1; ox++) {
                if (ox || oy) {
                    DrawChar(s, cx + ox * scale, y + oy * scale, scale, *p, outline);
                }
            }
        }
        cx += 6 * scale;
    }
    cx = x;
    for (const char* p = str; *p; p++) {
        DrawChar(s, cx, y, scale, *p, color);
        cx += 6 * scale;
    }
    return cx - x;
}

static int32_t TextWidthPx(const char* str, int32_t scale) {
    return (int32_t)strlen(str) * 6 * scale - scale;
}

/* ------------------------------------------------------------------ */
/*  Menu text (the game's stylized banner font)                        */
/* ------------------------------------------------------------------ */

/* Every panel label renders in the fat banner lettering (theme bank 8 —
 * the "South Hyrule Field" face: white body, silver shade, navy outline,
 * recolored per SS_TEXT_* on light surfaces). Metrics are in that font's
 * units: a glyph box is 16 rows tall at scale ms; ink fills nearly the
 * whole box, so vertical centering uses the box middle at 8*ms. The 5x7
 * stand-in only appears pre-ROM; it maps ms onto a similar visual size.
 * The face covers A-Z a-z 0-9 - . , : ' ! ? — keep strings inside that. */
#define MENU_TEXT_BOX 16

static int32_t FallbackScale5x7(int32_t ms) {
    int32_t f = ms * 2;
    return f < 1 ? 1 : f;
}

static int32_t MenuTextWidth(const char* str, int32_t ms) {
    int32_t w = Port_SecondScreenTheme_BigTextWidth(str, ms);
    if (w == 0 && str != NULL && *str != '\0') {
        w = TextWidthPx(str, FallbackScale5x7(ms));
    }
    return w;
}

/* yTop is the glyph-box top. Returns the advance. */
static int32_t MenuTextDraw(const SSurf* s, const char* str, int32_t x, int32_t yTop, int32_t ms,
                            int style) {
    int32_t adv =
        Port_SecondScreenTheme_DrawBigText(s->px, s->w, s->h, s->stride, x, yTop, ms, style, str);
    if (adv == 0 && str != NULL && *str != '\0') {
        /* Pre-ROM stand-in in the matching palette role. */
        static const int kColorId[SS_TEXT_STYLE_COUNT] = { SSC_MENU_INK, SSC_MENU_WHITE, SSC_MENU_RED,
                                                           SSC_RUPEE_GREEN, SSC_BANNER_NAVY };
        uint32_t color = Port_SecondScreenTheme_Color(kColorId[style >= 0 && style < SS_TEXT_STYLE_COUNT
                                                                  ? style
                                                                  : SS_TEXT_INK]);
        uint32_t outline = (style == SS_TEXT_INK || style == SS_TEXT_NAVY)
                               ? Port_SecondScreenTheme_Color(SSC_MENU_CREAM)
                               : Port_SecondScreenTheme_Color(SSC_MENU_BLACK);
        adv = DrawTextStr(s, str, x, yTop + 3 * ms, FallbackScale5x7(ms), color, outline);
    }
    return adv;
}

/* Centered helper: centers the string's caps on (cx, cy). */
static void MenuTextCentered(const SSurf* s, const char* str, float cx, float cy, int32_t ms, int style) {
    int32_t w = MenuTextWidth(str, ms);
    MenuTextDraw(s, str, (int32_t)(cx - w / 2.0f), (int32_t)(cy - 8 * ms), ms, style);
}

/* Panel header: the pause screens' red name chip, centered at cx with
 * its top at topY (over the slab's carved top band, like the game hangs
 * its own screen-name chip). */
static void DrawPanelHeaderChip(const SSurf* s, float cx, float topY, const char* title, int32_t ms,
                                float u) {
    int32_t tw = MenuTextWidth(title, ms);
    float h = MENU_TEXT_BOX * ms + 24 * u;
    float x0 = cx - tw / 2.0f - 24 * u, x1 = cx + tw / 2.0f + 24 * u;
    int32_t cts = (int32_t)(h / 26.0f);
    if (cts < 1) cts = 1;
    Port_SecondScreenTheme_DrawChip(s->px, s->w, s->h, s->stride, (int32_t)x0, (int32_t)topY,
                                    (int32_t)(x1 - x0), (int32_t)h, cts, SS_CHIP_RED);
    MenuTextCentered(s, title, cx, topY + h / 2.0f, ms, SS_TEXT_WHITE);
}

/* ------------------------------------------------------------------ */
/*  HUD-font numbers                                                   */
/* ------------------------------------------------------------------ */

/* Right-aligned counter in the HUD's own 8x16 digit font (yellow variant
 * for maxed counters, exactly like RenderDigits). Returns the left edge
 * reached so callers can place an icon in front. minDigits pads with
 * leading zeros the way the HUD pads the rupee counter. */
static int32_t DrawHudNumber(const SSurf* s, int32_t xRight, int32_t y, int32_t scale, uint32_t value,
                             int32_t minDigits, int yellow) {
    int32_t x = xRight;
    int32_t drawn = 0;
    do {
        const SecondScreenThemeSprite* d =
            Port_SecondScreenTheme_Get((yellow ? SST_DIGIT_YELLOW_0 : SST_DIGIT_WHITE_0) + (int)(value % 10));
        x -= 8 * scale;
        if (d != NULL) {
            BlitSprite(s, d, x, y, scale);
        } else {
            char digit[2] = { (char)('0' + value % 10), 0 };
            DrawTextStr(s, digit, x + scale, y + 2 * scale, scale + scale / 2,
                        Port_SecondScreenTheme_Color(SSC_MENU_INK),
                        Port_SecondScreenTheme_Color(SSC_MENU_CREAM));
        }
        value /= 10;
        drawn++;
    } while (value != 0 || drawn < minDigits);
    return x;
}

/* Two-glyph ammo count in the HUD's small 8x8 font (tens glyph is
 * right-aligned in its tile, ones left-aligned — sub_0801C2F0's pair). */
static void DrawAmmoCount(const SSurf* s, int32_t x, int32_t y, int32_t scale, uint32_t value) {
    if (value > 99) {
        value = 99;
    }
    const SecondScreenThemeSprite* tens = Port_SecondScreenTheme_Get(SST_SMALL_TENS_0 + (int)(value / 10));
    const SecondScreenThemeSprite* ones = Port_SecondScreenTheme_Get(SST_SMALL_ONES_0 + (int)(value % 10));
    if (tens == NULL || ones == NULL) {
        return; /* purely decorative — skip rather than substitute */
    }
    BlitSprite(s, tens, x, y, scale);
    BlitSprite(s, ones, x + 8 * scale, y, scale);
}

/* ------------------------------------------------------------------ */
/*  Menu button (tab bar + R glyph plate)                              */
/* ------------------------------------------------------------------ */

/* The pause menu's SLEEP/SAVE button, procedurally: pale near-white
 * plate, slate-blue outer keyline, a paler blue keyline inside it, blue
 * lettering. Only the stand-in for Port_SecondScreenTheme_DrawMenuButton
 * until that art decodes — same rect, same height, and `pressed` changes
 * nothing but the plate's shading, so a button never moves or resizes
 * between its states. Every tone is mixed from the decoded banner/menu
 * palette rather than invented. */
static void DrawButtonPlateFallback(const SSurf* s, float x0, float y0, float x1, float y1, int pressed,
                                    int32_t bts) {
    uint32_t navy = Port_SecondScreenTheme_Color(SSC_BANNER_NAVY);
    uint32_t white = Port_SecondScreenTheme_Color(SSC_MENU_WHITE);
    float bt = (float)bts;
    float r = 5.0f * bts;
    /* Pressed reads as the plate sinking: the interior takes a touch more
     * of the keyline's blue and the inner keyline brightens to white. */
    uint32_t keyline = MixColor(navy, white, 96);
    uint32_t inner = pressed ? white : MixColor(white, navy, 40);
    uint32_t fill = MixColor(white, navy, pressed ? 34u : 6u);
    FillRoundRect(s, x0, y0, x1, y1, r, keyline);
    FillRoundRect(s, x0 + bt, y0 + bt, x1 - bt, y1 - bt, r - bt, inner);
    FillRoundRect(s, x0 + 2 * bt, y0 + 2 * bt, x1 - 2 * bt, y1 - 2 * bt, r - 2 * bt, fill);
}

/* One tab-bar-style button: the game's own art when it decodes, the
 * stand-in plate otherwise, plus (for the tab that owns the panel) a thin
 * gold keyline inside the plate edge. Label drawing stays on this side
 * only for the fallback — the authentic call letters its own button. */
static void DrawMenuButton(const SSurf* s, float x0, float y0, float x1, float y1, const char* label,
                           int pressed, int accent, float u, int32_t ts) {
    int32_t bts = (int32_t)((y1 - y0) / 26.0f);
    if (bts < 1) bts = 1;
    if (bts > ts) bts = ts;

    if (!Port_SecondScreenTheme_DrawMenuButton(s->px, s->w, s->h, s->stride, (int32_t)x0, (int32_t)y0,
                                               (int32_t)(x1 - x0), (int32_t)(y1 - y0), label, pressed)) {
        DrawButtonPlateFallback(s, x0, y0, x1, y1, pressed, bts);
        if (label != NULL && label[0] != '\0') {
            int32_t ms = (int32_t)(1.8f * u);
            if (ms < 1) ms = 1;
            MenuTextCentered(s, label, (x0 + x1) / 2.0f, (y0 + y1) / 2.0f, ms, SS_TEXT_NAVY);
        }
    }
    if (accent) {
        StrokeRoundRect(s, x0 + 2 * bts, y0 + 2 * bts, x1 - 2 * bts, y1 - 2 * bts, 4.0f * bts,
                        (float)bts, Port_SecondScreenTheme_Color(SSC_GOLD));
    }
}

/* ------------------------------------------------------------------ */
/*  Pin + cog pixel art                                                */
/* ------------------------------------------------------------------ */

/* A windcrest warp point: a little pennant on a pole, foot at the marked
 * spot — drawn on a pixel grid so it sits next to the decoded HUD art
 * instead of looking like a vector overlay. (Original art.) */
static const char* const kPinArt[12] = {
    "########", "########", "#######.", "######..", "#####...", "###.....",
    "##......", "##......", "##......", "##......", "##......", "##......",
};

static void DrawPin(const SSurf* s, float x, float y, float cell, uint32_t color, uint32_t outline) {
    float px = x - 1 * cell, py = y - 12 * cell;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx || dy) DrawPixelArt(s, kPinArt, 12, px + dx * cell, py + dy * cell, cell, outline);
        }
    }
    DrawPixelArt(s, kPinArt, 12, px, py, cell, color);
}

/* Three sliders on a 9x9 grid — the settings-button glyph. A gear shape
 * reads as a blob at this size (same call the reference made). */
static const char* const kCogArt[9] = {
    ".###.....", "#########", ".###.....", ".....###.", "#########",
    ".....###.", "..###....", "#########", "..###....",
};

/* ------------------------------------------------------------------ */
/*  Target list                                                        */
/* ------------------------------------------------------------------ */

static void AddTarget(TargetList* tl, float x0, float y0, float x1, float y1, uint8_t action, uint8_t arg) {
    if (tl->n >= SS_MAX_TARGETS) {
        return;
    }
    TapTarget* t = &tl->t[tl->n++];
    t->x0 = (int32_t)x0;
    t->y0 = (int32_t)y0;
    t->x1 = (int32_t)x1;
    t->y1 = (int32_t)y1;
    t->action = action;
    t->arg = arg;
}

/* ------------------------------------------------------------------ */
/*  Cinema screen                                                      */
/* ------------------------------------------------------------------ */

/* Title screen / file select / cutscenes: nothing to mirror, so go quiet —
 * near-black, a thin dark-gold inset frame, and the four-element diamond
 * cluster breathing on a ~2.4 s sine in the middle. Deliberately minimal:
 * decorated idle cards were rejected on zelda3. */
static void PaintCinema(const SSurf* s, uint32_t tick) {
    FillRect(s, 0, 0, s->w, s->h, COL_IDLE_BG);

    float u = (s->w < s->h ? s->w : s->h) / 720.0f;
    int32_t inset = (int32_t)(12 * u);
    int32_t th = (int32_t)(2 * u) > 0 ? (int32_t)(2 * u) : 1;
    OutlineRect(s, inset, inset, s->w - inset, s->h - inset, th,
                MixColor(Port_SecondScreenTheme_Color(SSC_GOLD), COL_IDLE_BG, 140));

    /* 2.4 s period at the 20 Hz paint rate = 48 ticks. */
    float pulse = 0.55f + 0.30f * sinf((float)(tick % 48) * (6.28318f / 48.0f));
    int32_t cx = s->w / 2;
    int32_t cy = s->h / 2;
    int32_t r = (s->w < s->h ? s->w : s->h) / 32;
    int32_t d = r * 2 + r / 4;
    static const uint32_t kElem[4] = { COL_ELEM_WIND, COL_ELEM_FIRE, COL_ELEM_EARTH, COL_ELEM_WATER };
    static const int32_t kOff[4][2] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };
    for (int e = 0; e < 4; e++) {
        uint32_t c = kElem[e];
        uint32_t rr = (uint32_t)((c & 0xFF) * pulse);
        uint32_t gg = (uint32_t)(((c >> 8) & 0xFF) * pulse);
        uint32_t bb = (uint32_t)(((c >> 16) & 0xFF) * pulse);
        FillDiamond(s, cx + kOff[e][0] * d, cy + kOff[e][1] * d, r, RGB(rr, gg, bb));
    }
}

/* ------------------------------------------------------------------ */
/*  Schematic map (shared fallback)                                    */
/* ------------------------------------------------------------------ */

static void DrawMapMarker(const SSurf* s, int32_t px, int32_t py, int32_t unit, uint32_t color) {
    FillDiamond(s, px, py, unit + 1, Port_SecondScreenTheme_Color(SSC_MENU_INK));
    FillDiamond(s, px, py, unit, color);
}

/* Schematic area map — the styled fallback whenever the authentic art
 * modules report "not ready". Rooms placed by their real in-area geometry
 * (RoomResInfo), toned in the menu's stone/ink palette so it reads as part
 * of the pause-menu theme, not programmer art. In dungeons it obeys the
 * same reveal rule as the real map screen: unvisited rooms only appear
 * once the dungeon map item is owned. */
static void PaintSchematic(const SSurf* s, const SecondScreenSnapshot* snap, int32_t x0, int32_t y0,
                           int32_t x1, int32_t y1, uint32_t tick, int32_t unit) {
    uint32_t fill = Port_SecondScreenTheme_Color(SSC_MENU_STONE);
    uint32_t dark = Port_SecondScreenTheme_Color(SSC_MENU_INK);
    uint32_t colSeen = fill;
    uint32_t colUnseen = MixColor(fill, dark, 72);
    int isDungeon = (snap->areaFlags & SECOND_SCREEN_AR_IS_DUNGEON) != 0;
    int layoutKnown = !isDungeon || (snap->dungeonItemBits & 1) != 0;

    int32_t minX = INT32_MAX, minY = INT32_MAX, maxX = INT32_MIN, maxY = INT32_MIN;
    for (int i = 0; i < SECOND_SCREEN_MAX_ROOMS; i++) {
        const SecondScreenRoom* r = &snap->rooms[i];
        if (r->w == 0 || r->h == 0) {
            continue;
        }
        if (r->x < minX) minX = r->x;
        if (r->y < minY) minY = r->y;
        if (r->x + r->w > maxX) maxX = r->x + r->w;
        if (r->y + r->h > maxY) maxY = r->y + r->h;
    }
    if (minX >= maxX || minY >= maxY) {
        return;
    }

    float scaleX = (float)(x1 - x0) / (float)(maxX - minX);
    float scaleY = (float)(y1 - y0) / (float)(maxY - minY);
    float scale = scaleX < scaleY ? scaleX : scaleY;
    int32_t ox = x0 + (int32_t)((x1 - x0) - (maxX - minX) * scale) / 2;
    int32_t oy = y0 + (int32_t)((y1 - y0) - (maxY - minY) * scale) / 2;
    int32_t seam = unit > 2 ? unit / 2 : 1;

    for (int i = 0; i < SECOND_SCREEN_MAX_ROOMS; i++) {
        const SecondScreenRoom* r = &snap->rooms[i];
        if (r->w == 0 || r->h == 0) {
            continue;
        }
        bool visited = (snap->visitedMask >> (i & 63)) & 1;
        if (!visited && !layoutKnown) {
            continue; /* no map item -> the layout stays secret, like the real screen */
        }
        int32_t rx0 = ox + (int32_t)((r->x - minX) * scale);
        int32_t ry0 = oy + (int32_t)((r->y - minY) * scale);
        int32_t rx1 = ox + (int32_t)((r->x - minX + r->w) * scale);
        int32_t ry1 = oy + (int32_t)((r->y - minY + r->h) * scale);
        uint32_t roomFill = i == snap->room ? MixColor(Port_SecondScreenTheme_Color(SSC_GOLD), fill, 96)
                                            : visited ? colSeen : colUnseen;
        FillRect(s, rx0, ry0, rx1, ry1, dark);
        FillRect(s, rx0 + seam, ry0 + seam, rx1 - seam, ry1 - seam, roomFill);
    }

    int32_t px = ox + (int32_t)((snap->playerX - minX) * scale);
    int32_t py = oy + (int32_t)((snap->playerY - minY) * scale);
    DrawMapMarker(s, px, py, unit,
                  (tick & 8) ? RGB(0xF8, 0xF8, 0xF8) : Port_SecondScreenTheme_Color(SSC_GOLD));
}

/* ------------------------------------------------------------------ */
/*  Overworld map (MAP tab)                                            */
/* ------------------------------------------------------------------ */

/* Stone-frame bounding box within the decoded 240x160 map-screen
 * composite. The composite replicates the whole GBA screen, so it carries
 * the map screen's OWN parchment margins (plus their doodles) around the
 * frame; rendered over this panel's re-stamped parchment those margins
 * read as a hard halo band (owner feedback). Only the frame+map region is
 * ever sampled. Coordinates measured on the composed image: the frame's
 * outer outline spans x 16..222, y 3..147 inclusive (its rounded corners
 * leave sub-pixel cream slivers that match our backdrop cream anyway). */
#define WMAP_CROP_X0 16
#define WMAP_CROP_Y0 3
#define WMAP_CROP_X1 223
#define WMAP_CROP_Y1 148

/* Nearest-neighbor blit of the map image under transform (ox, oy, scale),
 * clipped to the given rect; samples only the frame crop of the source.
 * Incremental fixed-step sampling: one float add + cast per pixel. */
static void BlitMapRegion(const SSurf* s, const uint32_t* img, int32_t imgW, int32_t imgH, float ox,
                          float oy, float scale, int32_t cx0, int32_t cy0, int32_t cx1, int32_t cy1) {
    int32_t sxMin = WMAP_CROP_X0, syMin = WMAP_CROP_Y0;
    int32_t sxMax = imgW < WMAP_CROP_X1 ? imgW : WMAP_CROP_X1;
    int32_t syMax = imgH < WMAP_CROP_Y1 ? imgH : WMAP_CROP_Y1;
    if (cx0 < 0) cx0 = 0;
    if (cy0 < 0) cy0 = 0;
    if (cx1 > s->w) cx1 = s->w;
    if (cy1 > s->h) cy1 = s->h;
    if (scale <= 0.0f) return;
    float inv = 1.0f / scale;
    for (int32_t y = cy0; y < cy1; y++) {
        int32_t sy = (int32_t)((y - oy) * inv);
        if (sy < syMin || sy >= syMax) continue;
        const uint32_t* srow = img + (size_t)sy * (size_t)imgW;
        uint32_t* drow = s->px + (size_t)y * (size_t)s->stride;
        float sxf = (cx0 - ox) * inv;
        for (int32_t x = cx0; x < cx1; x++, sxf += inv) {
            int32_t sx = (int32_t)sxf;
            if (sx >= sxMin && sx < sxMax) {
                drow[x] = srow[sx];
            }
        }
    }
}

/* A small dark message chip with a banner-font word — the furniture the
 * game floats over its own map screen. Used for the map's view/back
 * affordance. Returns the rect it covered so callers can make it tappable. */
static void DrawMapChip(const SSurf* s, const char* label, float cx, float cyBottom, float u, float* out) {
    int32_t ms = (int32_t)(1.6f * u);
    if (ms < 1) ms = 1;
    int32_t tw = MenuTextWidth(label, ms);
    float ch = MENU_TEXT_BOX * ms + 16 * u;
    float x0 = cx - tw / 2.0f - 18 * u, x1 = cx + tw / 2.0f + 18 * u;
    float y1 = cyBottom, y0 = y1 - ch;
    int32_t cts = (int32_t)(ch / 24.0f);
    if (cts < 1) cts = 1;
    Port_SecondScreenTheme_DrawChip(s->px, s->w, s->h, s->stride, (int32_t)x0, (int32_t)y0,
                                    (int32_t)(x1 - x0), (int32_t)(y1 - y0), cts, SS_CHIP_DARK);
    MenuTextCentered(s, label, cx, (y0 + y1) / 2.0f, ms, SS_TEXT_WHITE);
    out[0] = x0;
    out[1] = y0;
    out[2] = x1;
    out[3] = y1;
}

/* The interactive overworld map, full-bleed in the map area: a gliding
 * follow-cam centered on Link, tap to toggle the whole-map fitted view,
 * and from the whole view a tap on a map tile brackets it and zooms into
 * that region; windcrest warp points draw as small pins. While Link is
 * indoors the marker holds the doorway fix. */
static void PaintOverworld(const SSurf* s, const SecondScreenSnapshot* snap, TargetList* tl, float rx0,
                           float ry0, float rx1, float ry1, float u, uint32_t tick, int wholeMap,
                           int followCfg, int crestCfg, uint32_t tickForPulse) {
    int32_t imgW = 0, imgH = 0;
    const uint32_t* img = Port_SecondScreenWorldMap_GetImage(&imgW, &imgH);
    if (img == NULL || imgW <= 0 || imgH <= 0) {
        PaintSchematic(s, snap, (int32_t)rx0, (int32_t)ry0, (int32_t)rx1, (int32_t)ry1, tick,
                       (int32_t)u > 0 ? (int32_t)u : 1);
        return;
    }

    int32_t mx, my;
    if (Port_SecondScreenWorldMap_LocatePlayer(snap->area, snap->playerX, snap->playerY, &mx, &my)) {
        sLastFix.valid = 1;
        sLastFix.mapX = mx;
        sLastFix.mapY = my;
    }

    /* All view math runs on the stone-frame crop, not the raw composite,
     * so the fitted whole view shows frame-to-frame with no margin halo
     * and the follow cam can never pan onto the composite's margins. */
    float cw = (float)(WMAP_CROP_X1 - WMAP_CROP_X0), chh = (float)(WMAP_CROP_Y1 - WMAP_CROP_Y0);
    float rw = rx1 - rx0, rh = ry1 - ry0;
    float wholeScale = (rw / cw < rh / chh) ? rw / cw : rh / chh;
    float followScale = wholeScale * 2.1f;

    /* Camera target: follow Link unless the whole map is asked for (or the
     * follow cam is switched off / there is no fix yet). */
    int wantWhole = wholeMap || !followCfg || !sLastFix.valid;
    float tScale = wantWhole ? wholeScale : followScale;
    float tx, ty;
    if (wantWhole) {
        tx = WMAP_CROP_X0 + cw / 2.0f;
        ty = WMAP_CROP_Y0 + chh / 2.0f;
    } else {
        float halfW = rw / (2.0f * tScale), halfH = rh / (2.0f * tScale);
        tx = (float)sLastFix.mapX;
        ty = (float)sLastFix.mapY;
        if (halfW * 2 < cw) {
            tx = tx < WMAP_CROP_X0 + halfW ? WMAP_CROP_X0 + halfW
                                           : (tx > WMAP_CROP_X1 - halfW ? WMAP_CROP_X1 - halfW : tx);
        } else {
            tx = WMAP_CROP_X0 + cw / 2.0f;
        }
        if (halfH * 2 < chh) {
            ty = ty < WMAP_CROP_Y0 + halfH ? WMAP_CROP_Y0 + halfH
                                           : (ty > WMAP_CROP_Y1 - halfH ? WMAP_CROP_Y1 - halfH : ty);
        } else {
            ty = WMAP_CROP_Y0 + chh / 2.0f;
        }
    }

    /* Smooth glide toward the target (pan and zoom both), snapping on the
     * first frame so a fresh surface doesn't animate in from nowhere. */
    if (!sCam.valid) {
        sCam.valid = 1;
        sCam.x = tx;
        sCam.y = ty;
        sCam.scale = tScale;
    } else {
        sCam.x += (tx - sCam.x) * 0.22f;
        sCam.y += (ty - sCam.y) * 0.22f;
        sCam.scale += (tScale - sCam.scale) * 0.22f;
    }

    float ox = (rx0 + rx1) / 2.0f - sCam.x * sCam.scale;
    float oy = (ry0 + ry1) / 2.0f - sCam.y * sCam.scale;
    BlitMapRegion(s, img, imgW, imgH, ox, oy, sCam.scale, (int32_t)rx0, (int32_t)ry0, (int32_t)rx1,
                  (int32_t)ry1);

    /* Map hints — the red checks and errand glyphs the game's own world map
     * shows — right above the map art, below the crest pins and the player
     * marker. Kinstone fusions deliberately do NOT belong here: the game
     * puts those on the enlarged region map, and so does PaintRegion. Both
     * worldmap calls degrade to 0 while their data/sprite isn't decodable,
     * and a marker simply skips that frame. */
    {
        SecondScreenMapMarker hints[16];
        int32_t nHints = Port_SecondScreenWorldMap_GetMapHints(snap->mapHints, hints, 16);
        int32_t hscale = (int32_t)(sCam.scale * 0.75f + 0.5f);
        if (hscale < 1) hscale = 1;
        for (int32_t i = 0; i < nHints; i++) {
            float px = ox + (hints[i].x + 0.5f) * sCam.scale;
            float py = oy + (hints[i].y + 0.5f) * sCam.scale;
            if (px >= rx0 && px < rx1 && py >= ry0 && py < ry1) {
                /* A marker stamp is a 16x16 frame drawn from its top-left
                 * (see port_second_screen_worldmap.h) — center on the spot. */
                Port_SecondScreenWorldMap_DrawMarker(s->px, s->w, s->h, s->stride,
                                                     (int32_t)(px - 8 * hscale),
                                                     (int32_t)(py - 8 * hscale), hscale, hints[i].frame,
                                                     SECOND_SCREEN_WORLDMAP_NO_REGION);
            }
        }
    }

    /* Windcrest warp points as small pins (green — the fast-travel accent),
     * gated by the settings toggle. Ids are the bit index within the
     * windcrests word's top byte, exactly what GetWindcrestPin expects. */
    if (crestCfg) {
        for (int32_t id = 0; id < 8; id++) {
            int32_t wx, wy;
            if (((snap->windcrests >> (24 + id)) & 1u) &&
                Port_SecondScreenWorldMap_GetWindcrestPin(id, &wx, &wy)) {
                float px = ox + (wx + 0.5f) * sCam.scale;
                float py = oy + (wy + 0.5f) * sCam.scale;
                if (px >= rx0 && px < rx1 && py >= ry0 && py < ry1) {
                    DrawPin(s, px, py, 0.8f * u, Port_SecondScreenTheme_Color(SSC_RUPEE_GREEN),
                            Port_SecondScreenTheme_Color(SSC_MENU_INK));
                }
            }
        }
    }

    /* Player marker: a pulsing gold diamond (live it also blinks white,
     * like the game's own map dot; the frozen doorway fix holds steady). */
    if (sLastFix.valid) {
        float px = ox + (sLastFix.mapX + 0.5f) * sCam.scale;
        float py = oy + (sLastFix.mapY + 0.5f) * sCam.scale;
        int32_t base =
            (int32_t)((6.5f + 1.5f * sinf((float)(tickForPulse % 32) * (6.28318f / 32.0f))) * u);
        uint32_t gold = Port_SecondScreenTheme_Color(SSC_GOLD);
        uint32_t c = (snap->areaFlags & SECOND_SCREEN_AR_IS_OVERWORLD)
                         ? ((tick & 8) ? RGB(0xF8, 0xF8, 0xF8) : gold)
                         : gold;
        DrawMapMarker(s, (int32_t)px, (int32_t)py, base, c);
    }

    /* Zoom-grid availability, asked once per frame at the view center: the
     * map screen's own tile grid answering means a tap can zoom, which is
     * also what decides whether the view chip below is worth showing. */
    int32_t probeRegion, probeRect[4];
    int gridReady = Port_SecondScreenWorldMap_GetRegionAt(
        WMAP_CROP_X0 + (int32_t)(cw / 2), WMAP_CROP_Y0 + (int32_t)(chh / 2), &probeRegion, &probeRect[0],
        &probeRect[1], &probeRect[2], &probeRect[3]);

    /* The tapped tile, bracketed on the map for a beat before the zoom —
     * the game's cursor brackets are the model, so the zoom reads as a
     * place you picked rather than a view that just swapped. */
    uint8_t regionState;
    float br[4];
    uint32_t sinceTap;
    UI_LOCK();
    regionState = sUi.regionState;
    br[0] = (float)sUi.regionX0;
    br[1] = (float)sUi.regionY0;
    br[2] = (float)sUi.regionX1;
    br[3] = (float)sUi.regionY1;
    sinceTap = tick - sUi.regionTick;
    UI_UNLOCK();
    if (regionState == SS_REGION_BRACKET) {
        float bx0 = ox + br[0] * sCam.scale, by0 = oy + br[1] * sCam.scale;
        float bx1 = ox + br[2] * sCam.scale, by1 = oy + br[3] * sCam.scale;
        float t = (float)u;
        if (t < 1) t = 1;
        StrokeRoundRect(s, bx0 - t, by0 - t, bx1 + t, by1 + t, 2 * t, t,
                        Port_SecondScreenTheme_Color(SSC_MENU_INK));
        StrokeRoundRect(s, bx0, by0, bx1, by1, 2 * t, t, Port_SecondScreenTheme_Color(SSC_GOLD));
        if (sinceTap >= SS_REGION_BRACKET_TICKS) {
            UI_LOCK();
            if (sUi.regionState == SS_REGION_BRACKET) {
                sUi.regionState = SS_REGION_VIEW;
            }
            UI_UNLOCK();
        }
    }

    /* Publish the transform (a tap becomes map-image coordinates for the
     * tile pick) and the map rect as one big view target. */
    UI_LOCK();
    sUi.mapLive = 1;
    sUi.mapOx = ox;
    sUi.mapOy = oy;
    sUi.mapScale = sCam.scale;
    sUi.mapU = u;
    sUi.mapImgW = imgW;
    sUi.mapImgH = imgH;
    sUi.regionGridReady = (uint8_t)(gridReady != 0);
    UI_UNLOCK();

    /* Once tiles are tappable a plain tap zooms, so the way back to the
     * follow cam gets its own chip — added ahead of the map's own target
     * so it wins the hit test. Before that the map keeps exactly the bare
     * tap-to-toggle it always had, with nothing extra on it. */
    if (gridReady && wantWhole && followCfg && sLastFix.valid) {
        float chip[4];
        DrawMapChip(s, "FOLLOW", (rx0 + rx1) / 2.0f, ry1 - 12 * u, u, chip);
        AddTarget(tl, chip[0], chip[1], chip[2], chip[3], SS_ACT_MAPVIEW, 0);
    }
    AddTarget(tl, rx0, ry0, rx1, ry1, SS_ACT_MAP, 0);
}

/* ------------------------------------------------------------------ */
/*  Regional map (MAP tab, after a tile zoom)                          */
/* ------------------------------------------------------------------ */

/* The enlarged map of one zoom-grid tile — the screen the game opens when
 * the player puts the map cursor on a tile — filling the map area, with the
 * markers that screen carries (map hints in their region glyphs, one glyph
 * per active kinstone fusion), Link's marker where that region places him
 * and a BACK chip in the game's own chip furniture. Returns 0 when the
 * region can't be drawn, and the caller simply stays on the world map (so a
 * not-ready art module can never leave the panel on a blank view). */
static int PaintRegion(const SSurf* s, const SecondScreenSnapshot* snap, TargetList* tl, float rx0,
                       float ry0, float rx1, float ry1, float u, uint32_t tick, int32_t region) {
    int32_t dx = (int32_t)rx0, dy = (int32_t)ry0;
    int32_t dw = (int32_t)(rx1 - rx0), dh = (int32_t)(ry1 - ry0);
    if (dw <= 0 || dh <= 0) {
        return 0;
    }

    /* Letterbox to the artwork's aspect instead of filling the rect. The map
     * area is close to square on this panel while the region art is not, so
     * stretching to fill visibly distorted the tiles — trees and bridges came
     * out elongated along whichever axis had the surplus.
     *
     * The fit is computed ONCE here and then used for the art, the markers and
     * Link's pin alike. All three scale by the destination size they are
     * handed, so fitting only the art would slide every marker off the terrain
     * it belongs to. */
    {
        int32_t artW = 0, artH = 0;
        if (Port_SecondScreenWorldMap_GetRegionSize(region, &artW, &artH) && artW > 0 && artH > 0) {
            /* Scale every region by the SAME factor rather than blowing each
             * one up to fill the panel. The regions differ enormously in size
             * (the sky is a fraction of Hyrule Field), and fit-to-fill turned
             * the small ones into a handful of enormous blocks. The game
             * itself draws them all at 1:1 inside one 240x160 frame, so take
             * that frame as the reference: the biggest region fills the map
             * area, the rest stay proportionally smaller — which also keeps
             * how much of Hyrule a region covers readable at a glance. */
            float refScale = (float)dw / 240.0f;
            float byH = (float)dh / 160.0f;
            if (byH < refScale) {
                refScale = byH;
            }
            int32_t fitW = (int32_t)(artW * refScale + 0.5f);
            int32_t fitH = (int32_t)(artH * refScale + 0.5f);
            if (fitW > dw) fitW = dw;
            if (fitH > dh) fitH = dh;
            if (fitW > 0 && fitH > 0) {
                dx += (dw - fitW) / 2;
                dy += (dh - fitH) / 2;
                dw = fitW;
                dh = fitH;
            }
        }
    }

    if (!Port_SecondScreenWorldMap_DrawRegion(s->px, s->w, s->h, s->stride, dx, dy, dw, dh, region)) {
        return 0;
    }

    /* A region carries few markers even fully fused — the busiest is Minish
     * Woods at 14 — so this is sized well past any real save; the game's own
     * marker buffer (gMapDataBottomSpecial) holds 128. */
    {
        SecondScreenMapMarker marks[48];
        int32_t n = Port_SecondScreenWorldMap_GetRegionMarkers(region, snap->mapHints,
                                                              snap->fusedKinstones, snap->fusionUnmarked,
                                                              dw, dh, marks, 48);
        /* The game stamps a 16 px marker on artwork it shows at 1:1; this
         * view fits the whole region instead, so the stamp is scaled by the
         * panel's own unit to read at about the same share of the map. */
        int32_t mscale = (int32_t)(2.5f * u + 0.5f);
        if (mscale < 1) mscale = 1;
        for (int32_t i = 0; i < n; i++) {
            Port_SecondScreenWorldMap_DrawMarker(s->px, s->w, s->h, s->stride,
                                                 dx + marks[i].x - 8 * mscale,
                                                 dy + marks[i].y - 8 * mscale, mscale, marks[i].frame,
                                                 region);
        }
    }

    int32_t px, py;
    if (Port_SecondScreenWorldMap_LocateInRegion(region, snap->area, snap->playerX, snap->playerY, dw, dh,
                                                 &px, &py)) {
        int32_t base = (int32_t)(7.0f * u);
        uint32_t gold = Port_SecondScreenTheme_Color(SSC_GOLD);
        DrawMapMarker(s, dx + px, dy + py, base, (tick & 8) ? RGB(0xF8, 0xF8, 0xF8) : gold);
    }

    float chip[4];
    DrawMapChip(s, "BACK", (rx0 + rx1) / 2.0f, ry1 - 12 * u, u, chip);
    AddTarget(tl, chip[0], chip[1], chip[2], chip[3], SS_ACT_MAPVIEW, 0);
    /* Anywhere else on the region map goes back too — the same "tap again"
     * gesture that opened it. */
    AddTarget(tl, rx0, ry0, rx1, ry1, SS_ACT_MAP, 0);
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Dungeon map (MAP tab, in dungeons)                                 */
/* ------------------------------------------------------------------ */

/* Dungeon mode: name banner up top, tappable floor plaques down the left
 * (tap previews that floor; the preview auto-returns to Link's floor after
 * a few seconds when that toggle is on; Link's own plaque keeps a gold
 * edge), and the authentic floor map full-bleed in the remaining space. */
static void PaintDungeon(const SSurf* s, const SecondScreenSnapshot* snap, TargetList* tl, float rx0,
                         float ry0, float rx1, float ry1, float u, int32_t ts, uint32_t tick,
                         int returnCfg) {
    SecondScreenDungeonMapInfo info;
    int haveInfo = Port_SecondScreenDungeonMap_GetInfo(snap->dungeonIdx, snap->area, snap->room, &info) &&
                   info.floorCount > 0;

    uint32_t gold = Port_SecondScreenTheme_Color(SSC_GOLD);

    if (!haveInfo) {
        PaintSchematic(s, snap, (int32_t)(rx0 + 8 * u), (int32_t)(ry0 + 8 * u), (int32_t)(rx1 - 8 * u),
                       (int32_t)(ry1 - 8 * u), tick, ts);
        UI_LOCK();
        sUi.playerFloorDisp = SS_NO_FLOOR;
        UI_UNLOCK();
        return;
    }

    /* Resolve (and possibly expire) the plaque preview under the lock:
     * it drops when Link changes floor or dungeon, and — with the
     * auto-return toggle on — after SS_FLOOR_RETURN_TICKS. */
    int preview;
    UI_LOCK();
    if (sUi.floorPreview != SS_NO_FLOOR) {
        if (sUi.previewDungeon != snap->dungeonIdx || sUi.previewBaseFloor != info.currentFloor ||
            (returnCfg && (uint32_t)(tick - sUi.floorPreviewTick) > SS_FLOOR_RETURN_TICKS)) {
            sUi.floorPreview = SS_NO_FLOOR;
        }
    }
    preview = sUi.floorPreview;
    sUi.playerFloorDisp = info.currentFloor;
    sUi.curDungeon = snap->dungeonIdx;
    UI_UNLOCK();

    int viewFloor = (preview != SS_NO_FLOOR) ? preview : info.currentFloor;
    if (viewFloor < 0) viewFloor = 0;
    if (viewFloor >= info.floorCount) viewFloor = info.floorCount - 1;

    /* Name banner, top center — the pause menu's red header chip. */
    float topY = ry0 + 12 * u;
    const char* name = snap->dungeonIdx < 7 ? kDungeonNames[snap->dungeonIdx] : NULL;
    if (name != NULL) {
        int32_t ms = (int32_t)(2.2f * u);
        if (ms < 1) ms = 1;
        int32_t tw = MenuTextWidth(name, ms);
        float cx = (rx0 + rx1) / 2.0f;
        float bx0 = cx - tw / 2.0f - 22 * u, bx1 = cx + tw / 2.0f + 22 * u;
        float by0 = topY, by1 = topY + MENU_TEXT_BOX * ms + 18 * u;
        int32_t cts = (int32_t)((by1 - by0) / 32.0f);
        if (cts < 1) cts = 1;
        Port_SecondScreenTheme_DrawChip(s->px, s->w, s->h, s->stride, (int32_t)bx0, (int32_t)by0,
                                        (int32_t)(bx1 - bx0), (int32_t)(by1 - by0), cts, SS_CHIP_RED);
        MenuTextCentered(s, name, cx, (by0 + by1) / 2.0f, ms, SS_TEXT_WHITE);
        topY = by1;
    }

    /* Floor plaques, topmost floor first, down the left side: menu chips
     * (the previewed floor turns the header red, the rest stay dark). */
    float pw = 100 * u, ph = 50 * u, pgap = 8 * u;
    float px0 = rx0 + 20 * u;
    float py0 = topY + 12 * u;
    int8_t topFloorNum = snap->dungeonIdx < 7 ? kDungeonTopFloor[snap->dungeonIdx] : 2;
    for (int i = 0; i < info.floorCount && i < 8; i++) {
        float y0 = py0 + i * (ph + pgap);
        int sel = (i == viewFloor);
        int isPlayers = (i == info.currentFloor);
        int32_t cts = (int32_t)(ph / 26.0f);
        if (cts < 1) cts = 1;
        Port_SecondScreenTheme_DrawChip(s->px, s->w, s->h, s->stride, (int32_t)px0, (int32_t)y0,
                                        (int32_t)pw, (int32_t)ph, cts,
                                        sel ? SS_CHIP_RED : SS_CHIP_DARK);
        /* Label: nF above ground, B(n) below — see kDungeonTopFloor. */
        int fl = topFloorNum - 2 - i;
        char label[6];
        if (fl >= 1) {
            snprintf(label, sizeof(label), "%dF", fl);
        } else {
            snprintf(label, sizeof(label), "B%d", 1 - fl);
        }
        int32_t ms = (int32_t)(1.7f * u);
        if (ms < 1) ms = 1;
        MenuTextCentered(s, label, px0 + pw / 2 + (isPlayers ? 5 * u : 0), y0 + ph / 2, ms,
                         SS_TEXT_WHITE);
        /* A small gold marker keeps Link's floor obvious while another one
         * is being previewed. */
        if (isPlayers) {
            DrawMapMarker(s, (int32_t)(px0 + 13 * u), (int32_t)(y0 + ph / 2), (int32_t)(3 * u), gold);
        }
        AddTarget(tl, px0, y0, px0 + pw, y0 + ph, SS_ACT_PLAQUE, (uint8_t)i);
    }

    /* The floor map, full-bleed in what's left right of the plaques. */
    float mx0 = px0 + pw + 20 * u;
    float my0 = topY + 10 * u;
    if (!Port_SecondScreenDungeonMap_Draw(s->px, s->w, s->h, s->stride, (int32_t)mx0, (int32_t)my0,
                                          (int32_t)(rx1 - 12 * u - mx0), (int32_t)(ry1 - 10 * u - my0),
                                          snap->dungeonIdx, viewFloor, snap->area, snap->room,
                                          snap->visitedMask, snap->dungeonItemBits, snap->playerX,
                                          snap->playerY, tick)) {
        PaintSchematic(s, snap, (int32_t)mx0, (int32_t)my0, (int32_t)(rx1 - 12 * u),
                       (int32_t)(ry1 - 10 * u), tick, ts);
    }
}

/* ------------------------------------------------------------------ */
/*  Items panel                                                        */
/* ------------------------------------------------------------------ */

/* ITEMS tab: the pause menu's 16 equip slots as a 4x4 touch grid on the
 * menu's stone slab — same slot order as the real Items screen, the
 * slab's own recessed wells as cells, real item icons, the real blinking
 * gold equip cursor and the HUD's A/B bubbles on the equipped slots. Tap
 * equips to A, hold to B — or to whichever slot an armed sidebar ring
 * selected (that cell breathes while armed). */
static void PaintItemsPanel(const SSurf* s, const SecondScreenSnapshot* snap, TargetList* tl, float rx0,
                            float ry0, float rx1, float ry1, float u, int32_t ts, uint32_t tick,
                            int armedRing) {
    Port_SecondScreenTheme_DrawPlate(s->px, s->w, s->h, s->stride, (int32_t)rx0, (int32_t)ry0,
                                     (int32_t)(rx1 - rx0), (int32_t)(ry1 - ry0), ts);
    float inset = 12 * ts;
    float ix0 = rx0 + inset, iy0 = ry0 + inset, ix1 = rx1 - inset, iy1 = ry1 - inset;

    /* Header: the menu's red chip, hung over the slab's top band exactly
     * like the pause screens hang theirs. */
    int32_t hms = (int32_t)(2.4f * u);
    if (hms < 1) hms = 1;
    DrawPanelHeaderChip(s, (rx0 + rx1) / 2.0f, iy0, "ITEMS", hms, u);
    iy0 += MENU_TEXT_BOX * hms + 32 * u;

    const int cols = 4, rows = 4;
    int32_t cellW = (int32_t)(ix1 - ix0) / cols;
    int32_t cellH = (int32_t)(iy1 - iy0) / rows;
    int32_t cell = cellW < cellH ? cellW : cellH;
    if (cell < 24) {
        return;
    }
    int32_t gx0 = (int32_t)(ix0 + ((ix1 - ix0) - cell * cols) / 2);
    int32_t gy0 = (int32_t)(iy0 + ((iy1 - iy0) - cell * rows) / 2);
    int32_t gap = cell / 12;
    int32_t seam = ts > 2 ? ts / 2 : 1;

    uint32_t stoneDark = Port_SecondScreenTheme_Color(SSC_MENU_STONE_DARK);

    const SecondScreenThemeSprite* cursor =
        Port_SecondScreenTheme_Get((tick & 8) ? SST_CURSOR_1 : SST_CURSOR_0);

    /* The cell an armed ring would overwrite breathes slowly (2.4 s). */
    int pulseSlot = -1;
    if (armedRing == 1) pulseSlot = snap->equippedSlotA;
    if (armedRing == 2) pulseSlot = snap->equippedSlotB;
    float pt = 0.5f + 0.5f * sinf((float)(tick % 48) * (6.28318f / 48.0f));

    for (int slot = 0; slot < SECOND_SCREEN_ITEM_SLOTS; slot++) {
        int32_t cx0 = gx0 + (slot % cols) * cell + gap;
        int32_t cy0 = gy0 + (slot / cols) * cell + gap;
        int32_t cx1 = cx0 + cell - 2 * gap;
        int32_t cy1 = cy0 + cell - 2 * gap;

        uint8_t itemId = snap->menuItems[slot];

        /* The slab's own recessed well; the game shows every slot's well
         * whether or not something is in it. Well scale follows the cell
         * so the 8 px rim never dominates small cells. */
        int32_t wts = (cx1 - cx0) / 40;
        if (wts < 1) wts = 1;
        if (wts > ts) wts = ts;
        Port_SecondScreenTheme_DrawWell(s->px, s->w, s->h, s->stride, cx0, cy0, cx1 - cx0, cy1 - cy0, wts);
        if (slot == pulseSlot) {
            /* Armed-ring breath: a gold wash over the well interior. */
            int32_t in = 3 * wts;
            FillRect(s, cx0 + in, cy0 + in, cx1 - in, cy1 - in,
                     MixColor(stoneDark, Port_SecondScreenTheme_Color(SSC_GOLD),
                              (uint32_t)(30 + 90 * pt)));
        }
        if (itemId == 0) {
            continue;
        }

        /* Bottles are containers; the icon players recognize is what's
         * inside (the pause menu makes the same substitution). */
        uint8_t iconId = itemId;
        if (slot >= 12) {
            uint8_t content = snap->bottleContents[slot - 12];
            if (content != 0) {
                iconId = content;
            }
        }
        int32_t iconScale = (cell - 2 * gap - 4 * seam) / 16;
        if (iconScale < 1) iconScale = 1;
        if (iconScale > 6) iconScale = 6; /* GBA icons turn to mush past 6x */
        int32_t iconX = (cx0 + cx1) / 2 - 8 * iconScale;
        int32_t iconY = (cy0 + cy1) / 2 - 8 * iconScale;
        Port_SecondScreenRender_DrawItemIcon(s->px, s->w, s->h, s->stride, iconX, iconY, iconScale, iconId);

        /* Ammo under bombs/bow — the HUD's own tiny digit pair. */
        if (itemId == ITEMID_BOMBS || itemId == ITEMID_REMOTE_BOMBS) {
            int32_t ss = (iconScale + 1) / 2;
            DrawAmmoCount(s, iconX, cy1 - seam - 8 * ss, ss, snap->bombCount);
        } else if (itemId == ITEMID_BOW || itemId == ITEMID_LIGHT_ARROW) {
            int32_t ss = (iconScale + 1) / 2;
            DrawAmmoCount(s, iconX, cy1 - seam - 8 * ss, ss, snap->arrowCount);
        }

        if (slot == snap->equippedSlotA || slot == snap->equippedSlotB) {
            bool isA = slot == snap->equippedSlotA;
            if (cursor != NULL) {
                int32_t cmax = cursor->w > cursor->h ? cursor->w : cursor->h;
                int32_t cs = (cell + cell / 4) / cmax;
                if (cs < 1) cs = 1;
                BlitSprite(s, cursor, (cx0 + cx1) / 2 - cursor->w * cs / 2,
                           (cy0 + cy1) / 2 - cursor->h * cs / 2, cs);
            } else {
                OutlineRect(s, cx0, cy0, cx1, cy1, seam * 2, Port_SecondScreenTheme_Color(SSC_GOLD));
            }
            const SecondScreenThemeSprite* badge =
                Port_SecondScreenTheme_Get(isA ? SST_BUTTON_A : SST_BUTTON_B);
            if (badge != NULL) {
                int32_t bmax = badge->w > badge->h ? badge->w : badge->h;
                int32_t bs = (cell / 3) / bmax;
                if (bs < 1) bs = 1;
                int32_t bx = isA ? cx0 - seam : cx1 + seam - badge->w * bs;
                BlitSprite(s, badge, bx, cy0 - seam, bs);
            } else {
                int32_t tagS = (int32_t)(1.4f * u) > 0 ? (int32_t)(1.4f * u) : 1;
                char tag[2] = { isA ? 'A' : 'B', 0 };
                int32_t tagX = isA ? cx0 + seam : cx1 - seam - 5 * tagS;
                DrawTextStr(s, tag, tagX, cy0 + seam, tagS, Port_SecondScreenTheme_Color(SSC_MENU_INK),
                            Port_SecondScreenTheme_Color(SSC_MENU_CREAM));
            }
        }

        AddTarget(tl, (float)cx0, (float)cy0, (float)cx1, (float)cy1, SS_ACT_ITEM, itemId);
    }
}

/* ------------------------------------------------------------------ */
/*  Quest status panel                                                 */
/* ------------------------------------------------------------------ */

/* QUEST tab: the pause screen's own quest status, reflowed for this panel by
 * port_second_screen_quest.c — same wells, same slot art, same clusters the
 * game groups its entries into, given a near-square panel instead of the GBA's
 * 3:2 screen. The furniture a passive panel has no use for is dropped there:
 * the QUEST STATUS banner, the L/R tab arrows (this panel has real tabs) and
 * the SLEEP / SAVE plates (nothing here can sleep or save).
 *
 * Everything below is the fallback for the frames before that screen's OBJ
 * tiles resolve: the same 4x4 well grid the ITEMS tab uses, so a cold start
 * shows the tab's contents instead of an empty slab.
 *
 *   row 0  kinstone bag (fused count) | heart pieces | sword skills | shells
 *          or the trophies that replace them
 *   row 1  the three carried quest items
 *   row 2  the four elements
 *   row 3  grip ring, bracelets, flippers
 */
static void PaintQuestPanel(const SSurf* s, const SecondScreenSnapshot* snap, float rx0, float ry0,
                            float rx1, float ry1, float u, int32_t ts, uint32_t tick) {
    if (Port_SecondScreenQuest_Draw(s->px, s->w, s->h, s->stride, (int32_t)rx0, (int32_t)ry0,
                                    (int32_t)(rx1 - rx0), (int32_t)(ry1 - ry0), snap, tick)) {
        return;
    }

    (void)tick; /* nothing here animates: the panel has no selection cursor */

    Port_SecondScreenTheme_DrawPlate(s->px, s->w, s->h, s->stride, (int32_t)rx0, (int32_t)ry0,
                                     (int32_t)(rx1 - rx0), (int32_t)(ry1 - ry0), ts);
    float inset = 12 * ts;
    float ix0 = rx0 + inset, iy0 = ry0 + inset, ix1 = rx1 - inset, iy1 = ry1 - inset;

    /* No header chip: the tab bar below already says QUEST, and the banner is
     * what used to eat the top of the slab. */

    const int cols = 4, rows = 4;
    int32_t cellW = (int32_t)(ix1 - ix0) / cols;
    int32_t cellH = (int32_t)(iy1 - iy0) / rows;
    int32_t cell = cellW < cellH ? cellW : cellH;
    if (cell < 24) {
        return;
    }
    int32_t gx0 = (int32_t)(ix0 + ((ix1 - ix0) - cell * cols) / 2);
    int32_t gy0 = (int32_t)(iy0 + ((iy1 - iy0) - cell * rows) / 2);
    int32_t gap = cell / 12;
    int32_t seam = ts > 2 ? ts / 2 : 1;

    /* What each of the sixteen cells holds. iconItem 0 means "empty well". */
    uint8_t iconItem[16];
    uint16_t count[16];
    uint8_t hasCount[16];
    memset(iconItem, 0, sizeof(iconItem));
    memset(count, 0, sizeof(count));
    memset(hasCount, 0, sizeof(hasCount));

    /* Cell 0: the Tingle trophy takes the bag's well once won; otherwise the
     * bag itself, and only when owned — pieces without the bag show nothing,
     * the same rule sub_080A5594 follows. The corner count is fusions made,
     * which is the number players actually track. */
    if (snap->tingleTrophy == 1) {
        iconItem[0] = ITEMID_QST_TINGLE_TROPHY;
    } else if (snap->kinstoneBagOwned) {
        iconItem[0] = ITEMID_KINSTONE_BAG;
        count[0] = snap->kinstoneFused;
        hasCount[0] = 1;
    }

    /* Cell 1: heart pieces, drawn as the quarter-heart sprite for the count
     * so a glance reads the fraction without needing the digits. */
    /* Cell 2: sword techniques, count only when any are known. */
    if (snap->swordSkills != 0) {
        count[2] = snap->swordSkills;
        hasCount[2] = 1;
    }

    /* Cell 3: the medal once won, else the shell counter. */
    if (snap->carlovMedal == 1) {
        iconItem[3] = ITEMID_QST_CARLOV_MEDAL;
    } else if (snap->shellsOwned != 0) {
        iconItem[3] = ITEMID_SHELLS;
        count[3] = snap->shells;
        hasCount[3] = 1;
    }

    /* Row 1: the carried quest items, in the snapshot's own tray order. */
    for (int i = 0; i < 3; i++) {
        iconItem[4 + i] = snap->questItems[i];
    }
    /* The spare cell carries the figurine tally, which the ROM screen has no
     * well for but the panel has room for. */
    if (snap->figurineCount != 0) {
        count[7] = snap->figurineCount;
        hasCount[7] = 1;
    }

    /* Row 2: the four elements. Row 3: grip ring, bracelets, flippers. */
    for (int i = 0; i < 4; i++) {
        if (snap->elements & (1u << i)) {
            iconItem[8 + i] = (uint8_t)(ITEMID_EARTH_ELEMENT + i);
        }
    }
    for (int i = 0; i < 3; i++) {
        if (snap->passives & (1u << i)) {
            iconItem[12 + i] = (uint8_t)(ITEMID_GRIP_RING + i);
        }
    }

    for (int slot = 0; slot < 16; slot++) {
        int32_t cx0 = gx0 + (slot % cols) * cell + gap;
        int32_t cy0 = gy0 + (slot / cols) * cell + gap;
        int32_t cx1 = cx0 + cell - 2 * gap;
        int32_t cy1 = cy0 + cell - 2 * gap;

        int32_t wts = (cx1 - cx0) / 40;
        if (wts < 1) wts = 1;
        if (wts > ts) wts = ts;
        Port_SecondScreenTheme_DrawWell(s->px, s->w, s->h, s->stride, cx0, cy0, cx1 - cx0, cy1 - cy0,
                                        wts);

        int32_t iconScale = (cell - 2 * gap - 4 * seam) / 16;
        if (iconScale < 1) iconScale = 1;
        if (iconScale > 6) iconScale = 6; /* GBA icons turn to mush past 6x */
        int32_t iconX = (cx0 + cx1) / 2 - 8 * iconScale;
        int32_t iconY = (cy0 + cy1) / 2 - 8 * iconScale;

        if (slot == 1) {
            /* Heart pieces: the HUD's own quarter-heart faces, so three of
             * four reads as a three-quarter heart rather than as "3". */
            int hp = snap->heartPieces > 3 ? 3 : snap->heartPieces;
            const SecondScreenThemeSprite* heart =
                Port_SecondScreenTheme_Get(SST_HEART_EMPTY + hp);
            if (heart != NULL) {
                int32_t hmax = heart->w > heart->h ? heart->w : heart->h;
                int32_t hs = (cell - 4 * gap) / (hmax > 0 ? hmax : 8);
                if (hs < 1) hs = 1;
                BlitSprite(s, heart, (cx0 + cx1) / 2 - heart->w * hs / 2,
                           (cy0 + cy1) / 2 - heart->h * hs / 2, hs);
            }
            continue;
        }
        if (slot == 2 && hasCount[2]) {
            /* Sword techniques have no inventory icon of their own — the
             * player's own sword stands in, with the technique count on it. */
            Port_SecondScreenRender_DrawItemIcon(s->px, s->w, s->h, s->stride, iconX, iconY, iconScale,
                                                 ITEMID_SMITH_SWORD);
        } else if (iconItem[slot] != 0) {
            Port_SecondScreenRender_DrawItemIcon(s->px, s->w, s->h, s->stride, iconX, iconY, iconScale,
                                                 iconItem[slot]);
        }

        if (hasCount[slot]) {
            int32_t ss = (iconScale + 1) / 2;
            if (ss < 1) ss = 1;
            DrawAmmoCount(s, iconX, cy1 - seam - 8 * ss, ss, count[slot]);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Settings panel                                                     */
/* ------------------------------------------------------------------ */

static const char* const kSettingLabels[SS_SET_COUNT] = {
    "TOP HUD",  "FOLLOW CAM",       "WINDCREST PINS", "FLOOR AUTO RETURN",  "MASTER VOLUME",
    "AUTOSAVE", "COLOR CORRECTION", "SHOW FPS",       "HOLD TO ADVANCE TEXT",
};

/* Master volume as the nearest cycle stop (0/25/50/75/100), read from the
 * live mixer exactly like the imgui slider does. */
static int GetVolumeStop(void) {
    int pct = (int)(Port_Audio_GetMasterVolume() * 100.0f + 12.5f) / 25 * 25;
    return pct < 0 ? 0 : (pct > 100 ? 100 : pct);
}

/* Current display state of a row: fills the value label and returns
 * nonzero when the row should wear the red "active" chip. */
static int GetSettingState(int row, char* out, int outCap) {
    int on = 0;
    const char* txt = "OFF";
    switch (row) {
        case SS_SET_TOP_HUD:
            /* The row states what the top screen DOES: SHOW is the (red)
             * default, HIDE hands vitals duty to this panel. */
            on = !Port_Config_GetHideTopHud();
            txt = on ? "SHOW" : "HIDE";
            break;
        case SS_SET_FOLLOW: on = Port_Config_GetSecondScreenFollowCam(); break;
        case SS_SET_CRESTS: on = Port_Config_GetSecondScreenCrestPins(); break;
        case SS_SET_FLOOR_RETURN: on = Port_Config_GetSecondScreenFloorReturn(); break;
        case SS_SET_VOLUME: {
            int pct = GetVolumeStop();
            snprintf(out, (size_t)outCap, "%d", pct);
            return pct > 0;
        }
        case SS_SET_AUTOSAVE: on = Port_QuickSave_AutoEnabled() != 0; break;
        case SS_SET_COLOR_CORRECTION: on = Port_Config_GetColorCorrection(); break;
        case SS_SET_SHOW_FPS: on = Port_Config_GetShowFps(); break;
        case SS_SET_HOLD_ADVANCE: on = Port_Config_GetHoldToAdvanceText(); break;
    }
    if (row != SS_SET_TOP_HUD) {
        txt = on ? "ON" : "OFF";
    }
    snprintf(out, (size_t)outCap, "%s", txt);
    return on;
}

/* SETTINGS tab: this panel's map toggles plus the port-wide switches the
 * F8 menu owns on the top screen, as tappable rows — wells on the slab,
 * banner-font labels, the value as a message chip on the right (red chip
 * = active, dark chip = off, the menu's own accent pairing). Rows size
 * themselves to the panel so all SS_SET_COUNT stay on screen and
 * tappable at every supported surface. */
static void PaintSettingsPanel(const SSurf* s, TargetList* tl, float rx0, float ry0, float rx1, float ry1,
                               float u, int32_t ts) {
    Port_SecondScreenTheme_DrawPlate(s->px, s->w, s->h, s->stride, (int32_t)rx0, (int32_t)ry0,
                                     (int32_t)(rx1 - rx0), (int32_t)(ry1 - ry0), ts);
    float inset = 12 * ts;
    float ix0 = rx0 + inset, iy0 = ry0 + inset, ix1 = rx1 - inset, iy1 = ry1 - inset;

    int32_t hms = (int32_t)(2.4f * u);
    if (hms < 1) hms = 1;
    int32_t wts = ts > 3 ? 3 : ts;

    DrawPanelHeaderChip(s, (rx0 + rx1) / 2.0f, iy0, "SETTINGS", hms, u);

    float gap = 8 * u;
    float y0 = iy0 + MENU_TEXT_BOX * hms + 32 * u;
    float rowH = ((iy1 - 6 * u - y0) - (SS_SET_COUNT - 1) * gap) / SS_SET_COUNT;
    if (rowH > 64 * u) rowH = 64 * u;
    int32_t rms = (int32_t)(rowH * 0.55f / MENU_TEXT_BOX);
    int32_t rmsMax = (int32_t)(1.8f * u);
    if (rmsMax < 1) rmsMax = 1;
    if (rms > rmsMax) rms = rmsMax;
    if (rms < 1) rms = 1;

    for (int i = 0; i < SS_SET_COUNT; i++) {
        float ry = y0 + i * (rowH + gap);
        float rl = ix0 + 10 * u, rr = ix1 - 10 * u;
        char val[8];
        int on = GetSettingState(i, val, sizeof(val));
        Port_SecondScreenTheme_DrawWell(s->px, s->w, s->h, s->stride, (int32_t)rl, (int32_t)ry,
                                        (int32_t)(rr - rl), (int32_t)rowH, wts);
        MenuTextDraw(s, kSettingLabels[i], (int32_t)(rl + 20 * u),
                     (int32_t)(ry + rowH / 2 - 8 * rms), rms, SS_TEXT_INK);
        /* Right-aligned value chip: sized for "SHOW" so every row's chip
         * ends flush and same-sized; the label centers inside. */
        {
            float ch = rowH - 8 * u;
            float cw = MenuTextWidth("SHOW", rms) + 24 * u;
            float cx1 = rr - 10 * u, cx0 = cx1 - cw;
            float cy0 = ry + (rowH - ch) / 2;
            int32_t cts = (int32_t)(ch / 24.0f);
            if (cts < 1) cts = 1;
            Port_SecondScreenTheme_DrawChip(s->px, s->w, s->h, s->stride, (int32_t)cx0, (int32_t)cy0,
                                            (int32_t)cw, (int32_t)ch, cts,
                                            on ? SS_CHIP_RED : SS_CHIP_DARK);
            MenuTextCentered(s, val, (cx0 + cx1) / 2.0f, cy0 + ch / 2.0f, rms, SS_TEXT_WHITE);
        }
        AddTarget(tl, rl, ry, rr, ry + rowH, SS_ACT_SETTING, (uint8_t)i);
    }
}

/* ------------------------------------------------------------------ */
/*  Sidebar                                                            */
/* ------------------------------------------------------------------ */

/* One equip ring: recessed stone disc, double gold ring, the equipped
 * item's icon, and the HUD's own A/B bubble as the badge. An armed ring
 * breathes. (The quest screen's circular sockets are the model.) */
static void DrawItemRing(const SSurf* s, const SecondScreenSnapshot* snap, TargetList* tl, float cx,
                         float cy, float r, int isA, int armed, float u, uint32_t tick) {
    uint32_t ink = Port_SecondScreenTheme_Color(SSC_MENU_INK);
    uint32_t cream = Port_SecondScreenTheme_Color(SSC_MENU_CREAM);
    uint32_t gold = Port_SecondScreenTheme_Color(SSC_GOLD);
    uint32_t goldDim = MixColor(gold, cream, 96);

    if (armed) {
        /* Slow gold breath around the ring while it waits for an item tap. */
        float pt = 0.5f + 0.5f * sinf((float)(tick % 48) * (6.28318f / 48.0f));
        FillRing(s, (int32_t)cx, (int32_t)cy, (int32_t)(r + 7 * u), (int32_t)(r + 3 * u),
                 MixColor(cream, gold, (uint32_t)(60 + 150 * pt)));
    }

    FillCircle(s, (int32_t)cx, (int32_t)cy, (int32_t)r,
               Port_SecondScreenTheme_Color(SSC_MENU_STONE_DARK));

    /* Icon first, rings after, and sized to fit INSIDE the gold band: the
     * item art is a 16x16 square, so what has to clear the band is its
     * diagonal, not its side (owner feedback — icons with corner ink, like
     * the sword, poked out of the ring). The innermost band edge sits at
     * r - 6u; keep a pixel of daylight inside that, then take the largest
     * integer nearest-neighbor scale whose circumscribed circle still
     * fits. Capped like the item grid, which also keeps the icon clear of
     * the shoulder badge. */
    uint8_t itemId = isA ? snap->equippedA : snap->equippedB;
    if (itemId >= ITEMID_BOTTLE1 && itemId < ITEMID_BOTTLE1 + 4) {
        uint8_t content = snap->bottleContents[itemId - ITEMID_BOTTLE1];
        if (content != 0) {
            itemId = content;
        }
    }
    if (itemId != 0) {
        float rFit = r - 7.0f * u;
        int32_t iconScale = (int32_t)(rFit / (8.0f * 1.41421356f));
        if (iconScale < 1) iconScale = 1;
        if (iconScale > 6) iconScale = 6; /* GBA icons turn to mush past 6x */
        Port_SecondScreenRender_DrawItemIcon(s->px, s->w, s->h, s->stride, (int32_t)(cx - 8 * iconScale),
                                             (int32_t)(cy - 8 * iconScale), iconScale, itemId);
    }

    FillRing(s, (int32_t)cx, (int32_t)cy, (int32_t)r, (int32_t)(r - 1.5f * u), ink);
    FillRing(s, (int32_t)cx, (int32_t)cy, (int32_t)(r - 3 * u), (int32_t)(r - 4.5f * u), goldDim);
    FillRing(s, (int32_t)cx, (int32_t)cy, (int32_t)(r - 4.5f * u), (int32_t)(r - 6 * u), gold);

    /* Button badge on the ring's top-right shoulder, sized to sit ON the
     * band rather than reach into the icon's space. */
    const SecondScreenThemeSprite* badge = Port_SecondScreenTheme_Get(isA ? SST_BUTTON_A : SST_BUTTON_B);
    float bx = cx + r * 0.71f, by = cy - r * 0.71f;
    if (badge != NULL) {
        int32_t bs = (int32_t)(r * 0.6f / badge->w + 0.5f);
        if (bs < 1) bs = 1;
        BlitSprite(s, badge, (int32_t)(bx - badge->w * bs / 2.0f), (int32_t)(by - badge->h * bs / 2.0f),
                   bs);
    } else {
        FillCircle(s, (int32_t)bx, (int32_t)by, (int32_t)(10 * u),
                   Port_SecondScreenTheme_Color(SSC_MENU_STONE_DARK));
        FillRing(s, (int32_t)bx, (int32_t)by, (int32_t)(10 * u), (int32_t)(8.5f * u), gold);
        char tag[2] = { isA ? 'A' : 'B', 0 };
        int32_t tfs = (int32_t)(1.6f * u) > 0 ? (int32_t)(1.6f * u) : 1;
        DrawTextStr(s, tag, (int32_t)(bx - 2.5f * tfs), (int32_t)(by - 3.5f * tfs), tfs,
                    Port_SecondScreenTheme_Color(SSC_MENU_INK), Port_SecondScreenTheme_Color(SSC_MENU_CREAM));
    }

    AddTarget(tl, cx - r, cy - r, cx + r, cy + r, SS_ACT_RING, isA ? 1 : 2);
}

/* ------------------------------------------------------------------ */
/*  R-button prompt                                                    */
/* ------------------------------------------------------------------ */

/* The words the HUD's own prompt art spells, by ActionRButton value
 * (include/player.h). Only the lettering stand-in: once the theme can
 * stamp the real label frames this table stops being reached. */
static const char* const kRActionWords[] = {
    NULL,  "CANCEL", "DROP", "THROW",  "READ", "CHECK", "OPEN",
    "SPEAK", "GRAB", "LIFT", "GROW", "SHRINK", "ROLL",
};

/* The game's contextual R prompt, on the panel because the player may be
 * running with the top HUD hidden. Laid out in a band whose height never
 * changes, so nothing below it moves when the prompt comes and goes; the
 * band simply stays empty when there is no action. Authentic art first
 * (the HUD's R glyph + the game's own label frame), then the panel's own
 * button plate lettered R plus the word in the banner font. */
static void DrawRPrompt(const SSurf* s, const SecondScreenSnapshot* snap, float x, float y, float w,
                        float bandH, float u, int32_t ts) {
    if (snap->rActionFrame == 0) {
        return;
    }
    int32_t ms = (int32_t)(2.6f * u);
    if (ms < 1) ms = 1;
    float gh = bandH - 8 * u; /* glyph height */
    float gw = gh * 1.35f;    /* shoulder buttons are wider than tall */
    float cy = y + bandH / 2;

    const char* word = NULL;
    if (snap->rActionId < (uint8_t)(sizeof(kRActionWords) / sizeof(kRActionWords[0]))) {
        word = kRActionWords[snap->rActionId];
    }

    /* Glyph + label center in the sidebar as one unit. The block is laid
     * out on the lettered width; the art path draws at the same origin
     * (its frames are the same words at a comparable size), so the two
     * paths land in the same place. */
    /* Rounded, not floored. The R sprite is 16 px tall, so flooring threw
     * away most of a whole pixel-step and the glyph came out a size smaller
     * than the band was built for — next to the A/B rings it read as an
     * afterthought. */
    int32_t scale = (int32_t)(gh / 16.0f + 0.5f);
    if (scale < 1) scale = 1;
    float labelW = word != NULL ? (float)MenuTextWidth(word, ms) : 0.0f;
    float gap = 8 * u;
    float total = gw + (labelW > 0 ? gap + labelW : 0);
    float gx = x + (w - total) / 2;
    if (gx < x) gx = x;

    if (!Port_SecondScreenTheme_DrawRButton(s->px, s->w, s->h, s->stride, (int32_t)gx,
                                            (int32_t)(cy - gh / 2), scale)) {
        int32_t bts = (int32_t)(gh / 26.0f);
        if (bts < 1) bts = 1;
        if (bts > ts) bts = ts;
        DrawButtonPlateFallback(s, gx, cy - gh / 2, gx + gw, cy + gh / 2, 0, bts);
        MenuTextCentered(s, "R", gx + gw / 2, cy, ms, SS_TEXT_NAVY);
    }

    float lx = gx + gw + gap;
    if (Port_SecondScreenTheme_DrawActionLabel(s->px, s->w, s->h, s->stride, (int32_t)lx,
                                               (int32_t)(cy - gh / 2), scale, snap->rActionFrame) == 0 &&
        word != NULL) {
        MenuTextDraw(s, word, (int32_t)lx, (int32_t)(cy - 8 * ms), ms, SS_TEXT_INK);
    }
}

/* Sidebar, right edge: hearts on top (the most-glanced info), the R
 * prompt band under them, the A/B equip rings centered in the middle, the
 * rupee/keys chip anchored to the bottom, just above the tab bar. */
static void PaintSidebar(const SSurf* s, const SecondScreenSnapshot* snap, TargetList* tl, float x,
                         float y, float w, float h, float u, int32_t ts, uint32_t tick, int armedRing) {
    int isDungeon = (snap->areaFlags & SECOND_SCREEN_AR_HAS_KEYS) != 0;

    /* Hearts: up to TMC's 20, two rows of 10, quarter-heart states exactly
     * like DrawHearts (health is in eighths; the HUD shows quarters). */
    int maxHearts = snap->maxHealth / 8;
    if (maxHearts > 20) maxHearts = 20;
    if (maxHearts < 1) maxHearts = 1;
    int cols = maxHearts > 10 ? 10 : maxHearts;
    int rows = (maxHearts + 9) / 10;
    int32_t hk = (int32_t)(w / 84.0f);
    if (hk < 1) hk = 1;
    float hx0 = x + (w - cols * 8 * hk) / 2;
    float hy = y + 4 * u;
    int quarters = snap->health == 1 ? 1 : snap->health / 2;
    int fullHearts = quarters / 4;
    int frac = quarters & 3;
    for (int i = 0; i < maxHearts; i++) {
        int state;
        if (i < fullHearts) {
            state = SST_HEART_FULL;
        } else if (i == fullHearts && frac != 0) {
            state = SST_HEART_Q1 + (frac - 1);
        } else {
            state = SST_HEART_EMPTY;
        }
        const SecondScreenThemeSprite* hs = Port_SecondScreenTheme_Get(state);
        int32_t hx = (int32_t)(hx0 + (i % 10) * 8 * hk);
        int32_t hyy = (int32_t)(hy + (i / 10) * 8 * hk);
        if (hs != NULL) {
            BlitSprite(s, hs, hx, hyy, hk);
        } else {
            uint32_t c = state == SST_HEART_EMPTY ? RGB(0x38, 0x20, 0x20)
                                                  : Port_SecondScreenTheme_Color(SSC_HEART_RED);
            FillRect(s, hx + hk, hyy + hk, hx + 7 * hk, hyy + 7 * hk, c);
        }
    }
    float vitalsBottom = hy + rows * 8 * hk + 4 * u;

    /* R prompt band, reserved whether or not there is a prompt so the
     * rings below never shift when one appears. */
    /* The prompt shares the sidebar with the A/B rings, and at 34u it was
     * dwarfed by them — this is the one contextual control on the panel, so
     * it gets a band it can actually be read in. */
    float rBandH = 76 * u;
    DrawRPrompt(s, snap, x, vitalsBottom, w, rBandH, u, ts);
    vitalsBottom += rBandH;

    /* Counters chip anchored to the bottom: rupees always, keys inside
     * key-bearing areas — a recessed stone well like the slab's tray.
     * Icons run at twice the heart scale and the digits half again, so
     * the chip reads from a couch like the rest of the panel. */
    int chipRows = isDungeon ? 2 : 1;
    int32_t ik = 2 * hk;               /* icon scale (16x16 art) */
    int32_t ck = hk + (hk + 1) / 2;    /* digit scale (8x16 HUD font) */
    int32_t chipTs = ts > 2 ? 2 : ts;
    float chipPad = 8 * chipTs + 4 * u;
    float chipH = chipRows * 16 * ik + (chipRows - 1) * 6 * u + 2 * chipPad;
    float chipY = y + h - chipH;
    Port_SecondScreenTheme_DrawWell(s->px, s->w, s->h, s->stride, (int32_t)x, (int32_t)chipY, (int32_t)w,
                                    (int32_t)chipH, chipTs);
    {
        float ry = chipY + chipPad;
        float ixl = x + chipPad;
        float ixr = x + w - chipPad;
        float dy = (16 * ik - 16 * ck) / 2.0f; /* digits centered on the icon row */
        BlitSprite(s, Port_SecondScreenTheme_Get(SST_RUPEE_WALLET0 + (snap->walletType & 3)), (int32_t)ixl,
                   (int32_t)ry, ik);
        DrawHudNumber(s, (int32_t)ixr, (int32_t)(ry + dy), ck, snap->rupees, 3,
                      snap->walletMax != 0 && snap->rupees >= snap->walletMax);
        if (isDungeon) {
            ry += 16 * ik + 6 * u;
            BlitSprite(s, Port_SecondScreenTheme_Get(SST_KEY), (int32_t)ixl, (int32_t)ry, ik);
            DrawHudNumber(s, (int32_t)ixr, (int32_t)(ry + dy), ck, snap->dungeonKeys, 2, 0);
        }
    }

    /* Equip rings, centered between the vitals and the chip: A above B.
     * Tap a ring to arm it; the next item-grid tap assigns that slot. */
    float mid = (vitalsBottom + chipY) / 2;
    float ringR = 60 * u;
    if (ringR > w / 2 - 14 * u) ringR = w / 2 - 14 * u;
    float quarter = (chipY - vitalsBottom) / 4 - 7 * u;
    if (ringR > quarter) ringR = quarter;
    if (ringR >= 10 * u) {
        float rcx = x + w / 2;
        DrawItemRing(s, snap, tl, rcx, mid - ringR - 6 * u, ringR, 1, armedRing == 1, u, tick);
        DrawItemRing(s, snap, tl, rcx, mid + ringR + 6 * u, ringR, 0, armedRing == 2, u, tick);
    }
}

/* ------------------------------------------------------------------ */
/*  Tab bar                                                            */
/* ------------------------------------------------------------------ */

/* One tab: the pause menu's labelled button (9.png's SLEEP/SAVE plate).
 * The tab that owns the panel is the SAME button at the SAME geometry,
 * drawn in the button's own pressed state and wearing a thin gold keyline
 * — owner feedback: the old red chip differed in look and height from its
 * neighbours. */
static void DrawTabButton(const SSurf* s, TargetList* tl, float x0, float y0, float x1, float y1,
                          const char* label, int active, int tabId, float u, int32_t ts) {
    DrawMenuButton(s, x0, y0, x1, y1, label, active, active, u, ts);
    AddTarget(tl, x0, y0, x1, y1, SS_ACT_TAB, (uint8_t)tabId);
}

/* Bottom tab bar: [QUEST][MAP][ITEMS] + the square settings button, all
 * one button family at one height. The 34u dead band underneath keeps
 * every button clear of the Android gesture zone (~30 real px). */
static void PaintTabBar(const SSurf* s, TargetList* tl, float u, int32_t ts, int activeTab) {
    float tabH = 96 * u;
    float y = s->h - tabH + 4 * u;
    float bh = tabH - 34 * u;
    float sq = bh;
    float sx1 = s->w - 8 * u;
    float sx0 = sx1 - sq;
    float x0 = 8 * u, xr = sx0 - 8 * u, gap = 8 * u;
    float bw = (xr - x0 - 2 * gap) / 3.0f;

    DrawTabButton(s, tl, x0, y, x0 + bw, y + bh, "QUEST", activeTab == SS_TAB_QUEST, SS_TAB_QUEST, u, ts);
    DrawTabButton(s, tl, x0 + bw + gap, y, x0 + 2 * bw + gap, y + bh, "MAP", activeTab == SS_TAB_MAP,
                  SS_TAB_MAP, u, ts);
    DrawTabButton(s, tl, x0 + 2 * (bw + gap), y, x0 + 3 * bw + 2 * gap, y + bh, "ITEMS",
                  activeTab == SS_TAB_ITEMS, SS_TAB_ITEMS, u, ts);
    /* Settings keeps its cog glyph instead of a word, on the same plate —
     * an empty label, not a null one, so the art path never has to guess. */
    DrawTabButton(s, tl, sx0, y, sx1, y + bh, "", activeTab == SS_TAB_SETTINGS, SS_TAB_SETTINGS, u, ts);
    float cog = bh * 0.5f / 9.0f;
    DrawPixelArt(s, kCogArt, 9, (sx0 + sx1) / 2 - 4.5f * cog, y + bh / 2 - 4.5f * cog, cog,
                 Port_SecondScreenTheme_Color(SSC_BANNER_NAVY));
}

/* ------------------------------------------------------------------ */
/*  Frame composition                                                  */
/* ------------------------------------------------------------------ */

void Port_SecondScreen_PaintInto(uint32_t* pixels, int width, int height, int strideInPixels,
                                 const SecondScreenSnapshot* snap, uint32_t tick) {
    SSurf s = { pixels, width, height, strideInPixels };
    if (pixels == NULL || width <= 0 || height <= 0) {
        return;
    }

    if (!snap->inGame) {
        UI_LOCK();
        sTapTargetCount = 0; /* no gameplay, no touch UI */
        sUi.mapLive = 0;
        sUi.armedRing = 0;
        sUi.floorPreview = SS_NO_FLOOR;
        sUi.playerFloorDisp = SS_NO_FLOOR;
        sUi.regionState = SS_REGION_OFF; /* a zoom never survives a load */
        UI_UNLOCK();
        sLastFix.valid = 0; /* stale fixes must not survive into a new save */
        sCam.valid = 0;
        PaintCinema(&s, tick);
        return;
    }

    /* Theme decode happens lazily here — only during gameplay, when the
     * ROM tables are guaranteed resolved. Until it reports ready
     * everything below still renders through its per-element fallbacks. */
    Port_SecondScreenTheme_Ready();

    int isDungeon = (snap->areaFlags & SECOND_SCREEN_AR_IS_DUNGEON) != 0;

    int tab, armedRing, regionState;
    int32_t regionId;
    UI_LOCK();
    if (isDungeon) {
        sUi.regionState = SS_REGION_OFF; /* the world map is gone; so is its zoom */
    }
    tab = sUi.tab;
    armedRing = sUi.armedRing;
    int wholeMap = sUi.wholeMap;
    regionState = sUi.regionState;
    regionId = sUi.regionId;
    sUi.mapLive = 0; /* set again by PaintOverworld when the map is up */
    UI_UNLOCK();

    int followCfg = Port_Config_GetSecondScreenFollowCam();
    int crestCfg = Port_Config_GetSecondScreenCrestPins();
    int returnCfg = Port_Config_GetSecondScreenFloorReturn();

    /* Scale units: u drives the layout (reference design is a 720p min
     * axis), ts is the integer art scale for the decoded menu dressing. */
    float u = (width < height ? width : height) / 720.0f;
    int32_t minAxis = width < height ? width : height;
    int32_t ts = minAxis / 240;
    if (ts < 2) ts = 2;
    if (ts > 6) ts = 6;

    /* The whole surface is the pause menu's parchment; panels lay their
     * slab/chips over it. (Flat cream until the pattern decodes.) */
    Port_SecondScreenTheme_DrawBackdrop(s.px, s.w, s.h, s.stride, 0, 0, s.w, s.h, ts);

    float tabH = 96 * u;
    float sideW = 220 * u; /* widened for the grown rings/chip; map stays dominant */
    float mx0 = 10 * u, my0 = 10 * u;
    float mx1 = width - sideW - 4 * u;
    float my1 = height - tabH - 4 * u;

    TargetList tl = { .n = 0 };

    if (tab == SS_TAB_ITEMS) {
        PaintItemsPanel(&s, snap, &tl, mx0, my0, mx1, my1, u, ts, tick, armedRing);
    } else if (tab == SS_TAB_QUEST) {
        PaintQuestPanel(&s, snap, mx0, my0, mx1, my1, u, ts, tick);
    } else if (tab == SS_TAB_SETTINGS) {
        PaintSettingsPanel(&s, &tl, mx0, my0, mx1, my1, u, ts);
    } else if (isDungeon) {
        PaintDungeon(&s, snap, &tl, mx0, my0, mx1, my1, u, ts, tick, returnCfg);
    } else {
        /* A live region zoom replaces the map area; if its art can't be
         * drawn the view drops straight back to the world map, so the
         * panel is never left on a blank region. */
        int drewRegion = 0;
        if (regionState == SS_REGION_VIEW) {
            drewRegion = PaintRegion(&s, snap, &tl, mx0, my0, mx1, my1, u, tick, regionId);
            if (!drewRegion) {
                UI_LOCK();
                sUi.regionState = SS_REGION_OFF;
                UI_UNLOCK();
            }
        }
        if (!drewRegion) {
            PaintOverworld(&s, snap, &tl, mx0, my0, mx1, my1, u, tick, wholeMap, followCfg, crestCfg,
                           tick);
        }
    }

    PaintSidebar(&s, snap, &tl, width - sideW + 4 * u, 10 * u, sideW - 14 * u, height - tabH - 14 * u, u,
                 ts, tick, armedRing);
    PaintTabBar(&s, &tl, u, ts, tab);

    /* Publish this frame's hit boxes for the tap thread. */
    UI_LOCK();
    memcpy(sTapTargets, tl.t, sizeof(TapTarget) * (size_t)tl.n);
    sTapTargetCount = tl.n;
    sUi.lastTick = tick;
    UI_UNLOCK();
}

/* ------------------------------------------------------------------ */
/*  Tap handling                                                       */
/* ------------------------------------------------------------------ */

/* Tap on the whole-Hyrule map: turn screen pixels into map-image
 * coordinates through the transform of the last painted frame, ask the
 * map screen's own zoom grid which tile that is, and — when it answers —
 * bracket that tile for a beat before the regional map opens. Returns 0
 * when nothing was picked (off-grid, or the grid isn't decodable), which
 * leaves the caller's plain follow/whole toggle in charge. */
static int PickMapRegion(int x, int y) {
    int picked = 0;
    UI_LOCK();
    if (sUi.mapLive && sUi.regionGridReady && sUi.mapScale > 0.0f) {
        int32_t ix = (int32_t)(((float)x - sUi.mapOx) / sUi.mapScale);
        int32_t iy = (int32_t)(((float)y - sUi.mapOy) / sUi.mapScale);
        int32_t region, rx0, ry0, rx1, ry1;
        /* Only the frame crop ever renders, so a tap on the letterbox
         * around the fitted view is not a tap on the map. */
        if (ix >= WMAP_CROP_X0 && iy >= WMAP_CROP_Y0 && ix < WMAP_CROP_X1 && iy < WMAP_CROP_Y1 &&
            ix < sUi.mapImgW && iy < sUi.mapImgH &&
            Port_SecondScreenWorldMap_GetRegionAt(ix, iy, &region, &rx0, &ry0, &rx1, &ry1)) {
            sUi.regionId = region;
            sUi.regionX0 = rx0;
            sUi.regionY0 = ry0;
            sUi.regionX1 = rx1;
            sUi.regionY1 = ry1;
            sUi.regionTick = sUi.lastTick;
            sUi.regionState = SS_REGION_BRACKET;
            picked = 1;
        }
    }
    UI_UNLOCK();
    return picked;
}

void Port_SecondScreen_OnTap(int x, int y, int longPress) {
    TapTarget hit;
    int found = 0;

    UI_LOCK();
    for (int i = 0; i < sTapTargetCount; i++) {
        const TapTarget* t = &sTapTargets[i];
        if (x >= t->x0 && x < t->x1 && y >= t->y0 && y < t->y1) {
            hit = *t;
            found = 1;
            break;
        }
    }
    UI_UNLOCK();
    if (!found) {
        return;
    }

    switch (hit.action) {
        case SS_ACT_TAB:
            UI_LOCK();
            /* Re-tapping the active panel tab returns to the map — the
             * reference's "toggle back" behavior. */
            sUi.tab = (sUi.tab == hit.arg && hit.arg != SS_TAB_MAP) ? SS_TAB_MAP : hit.arg;
            sUi.armedRing = 0; /* changing tabs cancels a pending assignment */
            UI_UNLOCK();
            break;
        case SS_ACT_RING:
            UI_LOCK();
            sUi.armedRing = (sUi.armedRing == hit.arg) ? 0 : hit.arg;
            UI_UNLOCK();
            break;
        case SS_ACT_ITEM: {
            uint8_t armed;
            UI_LOCK();
            armed = sUi.armedRing;
            sUi.armedRing = 0;
            UI_UNLOCK();
            /* An armed ring picks the slot; otherwise tap = A, hold = B —
             * both through the engine-side RequestEquip flow. */
            uint8_t slot = armed ? (uint8_t)(armed - 1) : (uint8_t)(longPress ? 1 : 0);
            Port_SecondScreenState_RequestEquip(hit.arg, slot);
            break;
        }
        case SS_ACT_PLAQUE:
            UI_LOCK();
            if ((int8_t)hit.arg == sUi.playerFloorDisp) {
                sUi.floorPreview = SS_NO_FLOOR; /* tap Link's floor: back to live */
            } else {
                sUi.floorPreview = (int8_t)hit.arg;
                sUi.floorPreviewTick = sUi.lastTick;
                sUi.previewBaseFloor = sUi.playerFloorDisp;
                sUi.previewDungeon = sUi.curDungeon;
            }
            UI_UNLOCK();
            break;
        case SS_ACT_SETTING:
            switch (hit.arg) {
                case SS_SET_TOP_HUD:
                    /* Just the flag — the engine-side DrawUIElements gate
                     * reads it each frame. */
                    Port_Config_SetHideTopHud(!Port_Config_GetHideTopHud());
                    break;
                case SS_SET_FOLLOW:
                    Port_Config_SetSecondScreenFollowCam(!Port_Config_GetSecondScreenFollowCam());
                    break;
                case SS_SET_CRESTS:
                    Port_Config_SetSecondScreenCrestPins(!Port_Config_GetSecondScreenCrestPins());
                    break;
                case SS_SET_FLOOR_RETURN:
                    Port_Config_SetSecondScreenFloorReturn(!Port_Config_GetSecondScreenFloorReturn());
                    break;
                case SS_SET_VOLUME: {
                    /* Cycle 0 -> 25 -> 50 -> 75 -> 100 -> 0, applied to
                     * the live mixer + persisted — the same call pair as
                     * the imgui volume slider. */
                    float v = (float)((GetVolumeStop() + 25) % 125) / 100.0f;
                    Port_Audio_SetMasterVolume(v);
                    Port_Config_SetMasterVolume(v);
                    break;
                }
                case SS_SET_AUTOSAVE: {
                    /* Live autosaver toggle + persisted flag — the imgui
                     * checkbox's exact pair. */
                    int on = !Port_QuickSave_AutoEnabled();
                    Port_QuickSave_SetAutoEnabled(on);
                    Port_Config_SetAutosaveEnabled(on != 0);
                    break;
                }
                case SS_SET_COLOR_CORRECTION: {
                    /* Persisted flag (whose config DEFAULT is itself
                     * #ifdef __ANDROID__-guarded — off on device, on
                     * elsewhere; reading the getter tracks either) plus
                     * the PPU's live switch. */
                    bool on = !Port_Config_GetColorCorrection();
                    Port_Config_SetColorCorrection(on);
                    Port_PPU_SetColorCorrection(on);
                    break;
                }
                case SS_SET_SHOW_FPS:
                    /* The FPS overlay polls the flag every frame. */
                    Port_Config_SetShowFps(!Port_Config_GetShowFps());
                    break;
                case SS_SET_HOLD_ADVANCE:
                    /* src/message.c polls the flag at every advance. */
                    Port_Config_SetHoldToAdvanceText(!Port_Config_GetHoldToAdvanceText());
                    break;
            }
            break;
        case SS_ACT_MAP: {
            /* Region view: any tap goes back up a level. Whole map: a tap
             * on a zoom-grid tile zooms in; anything else keeps the
             * long-standing follow/whole toggle. Follow cam: toggle. */
            uint8_t state;
            int whole;
            UI_LOCK();
            state = sUi.regionState;
            whole = sUi.wholeMap;
            UI_UNLOCK();
            if (state != SS_REGION_OFF) {
                UI_LOCK();
                sUi.regionState = SS_REGION_OFF;
                UI_UNLOCK();
                break;
            }
            if (whole && PickMapRegion(x, y)) {
                break;
            }
            UI_LOCK();
            sUi.wholeMap = !sUi.wholeMap;
            UI_UNLOCK();
            break;
        }
        case SS_ACT_MAPVIEW:
            /* The map's own back step: out of the region view first, then
             * out of the whole map back to the follow cam. */
            UI_LOCK();
            if (sUi.regionState != SS_REGION_OFF) {
                sUi.regionState = SS_REGION_OFF;
            } else {
                sUi.wholeMap = 0;
            }
            UI_UNLOCK();
            break;
    }
}

/* ------------------------------------------------------------------ */
/*  Android surface plumbing                                           */
/* ------------------------------------------------------------------ */

#ifdef __ANDROID__

#include <android/native_window.h>
#include <time.h>

static ANativeWindow* sWindow = NULL;
static pthread_mutex_t sWindowMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t sRenderThread;
static bool sRenderThreadStarted = false;

static void PaintFrame(ANativeWindow* window) {
    static uint32_t sTick = 0;
    sTick++;

    ANativeWindow_Buffer buf;
    if (ANativeWindow_lock(window, &buf, NULL) != 0) {
        fprintf(stderr, "[second_screen] ANativeWindow_lock failed\n");
        return;
    }
    /* Java pins the SurfaceHolder to RGBA_8888, but verify rather than trust
     * across the JNI boundary — a format mismatch here (e.g. 2-bytes/pixel
     * RGB_565) would overflow these 4-bytes/pixel writes and crash native. */
    if (buf.format != WINDOW_FORMAT_RGBA_8888 && buf.format != WINDOW_FORMAT_RGBX_8888) {
        fprintf(stderr, "[second_screen] unexpected buffer format %d, skipping paint\n", buf.format);
        ANativeWindow_unlockAndPost(window);
        return;
    }

    SecondScreenSnapshot snap;
    Port_SecondScreenState_Read(&snap);
    Port_SecondScreen_PaintInto((uint32_t*)buf.bits, buf.width, buf.height, buf.stride, &snap, sTick);
    ANativeWindow_unlockAndPost(window);
}

static void* RenderThreadMain(void* arg) {
    (void)arg;
    fprintf(stderr, "[second_screen] render thread started\n");

    for (;;) {
        pthread_mutex_lock(&sWindowMutex);
        if (sWindow) {
            PaintFrame(sWindow);
        }
        pthread_mutex_unlock(&sWindowMutex);

        struct timespec sleepTime = { 0, 50 * 1000 * 1000 }; /* 50ms ~= 20 Hz */
        nanosleep(&sleepTime, NULL);
    }
    return NULL;
}

/* Started lazily on the first surface, then runs for the process lifetime,
 * idling (no window held -> no paint, just the sleep) across surface
 * loss/recreation rather than being torn down and rebuilt each time —
 * simpler than thread lifecycle management for what's still a HUD panel. */
static void EnsureRenderThreadStarted(void) {
    if (!sRenderThreadStarted) {
        pthread_create(&sRenderThread, NULL, RenderThreadMain, NULL);
        pthread_detach(sRenderThread);
        sRenderThreadStarted = true;
    }
}

void Port_SecondScreen_Init(void) {
    fprintf(stderr, "[second_screen] init\n");
}

void Port_SecondScreen_OnSurfaceReady(void* window, int width, int height) {
    ANativeWindow* nativeWindow = (ANativeWindow*)window;
    fprintf(stderr, "[second_screen] surface ready %dx%d\n", width, height);

    pthread_mutex_lock(&sWindowMutex);
    if (sWindow) {
        ANativeWindow_release(sWindow);
    }
    sWindow = nativeWindow;
    pthread_mutex_unlock(&sWindowMutex);

    EnsureRenderThreadStarted();
}

void Port_SecondScreen_OnSurfaceLost(void) {
    pthread_mutex_lock(&sWindowMutex);
    if (sWindow) {
        ANativeWindow_release(sWindow);
        sWindow = NULL;
    }
    pthread_mutex_unlock(&sWindowMutex);
    fprintf(stderr, "[second_screen] surface lost\n");
}

#else /* !__ANDROID__ — no second display; surface entry points are no-ops
       * (the compositor + tap handler above still compile and run, which
       * is what the host harness drives). */

void Port_SecondScreen_Init(void) {}
void Port_SecondScreen_OnSurfaceReady(void* window, int width, int height) {
    (void)window;
    (void)width;
    (void)height;
}
void Port_SecondScreen_OnSurfaceLost(void) {}

#endif
