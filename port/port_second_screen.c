#include "port_second_screen.h"
#include "port_second_screen_dungeonmap.h"
#include "port_second_screen_render.h"
#include "port_second_screen_state.h"
#include "port_second_screen_theme.h"
#include "port_second_screen_worldmap.h"

#include <stdio.h>

#ifdef __ANDROID__

#include <android/native_window.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

static ANativeWindow* sWindow = NULL;
static pthread_mutex_t sWindowMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t sRenderThread;
static bool sRenderThreadStarted = false;

/* Buffer pixel layout is RGBA_8888: as a little-endian u32 that's
 * A<<24 | B<<16 | G<<8 | R (same convention Rgb555ToRgba8888 in
 * port_second_screen_render.c emits). Build every color through this so
 * channel order mistakes can't creep in per call site. */
#define RGB(r, g, b) (0xFF000000u | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r))

/* The only colors of our own on the panel: the dark backdrop the TMC
 * windows float on, and the idle screen's four-element hues. Everything
 * inside the windows comes from the decoded theme
 * (port_second_screen_theme.c). */
#define COL_BG RGB(0x0E, 0x10, 0x0C)      /* dark olive-black backdrop */
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
#define ITEMID_DUNGEON_MAP 0x50
#define ITEMID_COMPASS 0x51
#define ITEMID_BIG_KEY 0x52
#define ITEMID_KINSTONE 0x5C

/* Tap targets of the most recently painted frame — the item grid moves
 * with surface size, so hit boxes are captured at paint time and consumed
 * by Port_SecondScreen_OnTap on the JNI thread. Own mutex (not
 * sWindowMutex): taps must never wait out a whole frame paint. */
typedef struct {
    int32_t x0, y0, x1, y1;
    uint8_t itemId;
} TapTarget;
static TapTarget sTapTargets[SECOND_SCREEN_ITEM_SLOTS];
static int sTapTargetCount = 0;
static pthread_mutex_t sTapTargetMutex = PTHREAD_MUTEX_INITIALIZER;

/* Last successful world-map position fix. While Link is indoors (houses,
 * caves — anywhere LocatePlayer has no answer) the map keeps showing this
 * frozen fix, zelda3-android's doorway-marker behavior. Render-thread
 * private; cleared when gameplay ends. */
static struct {
    int valid;
    int32_t mapX, mapY;
} sLastFix = { 0, 0, 0 };

/* ------------------------------------------------------------------ */
/*  Primitive helpers                                                  */
/* ------------------------------------------------------------------ */

static void FillRect(const ANativeWindow_Buffer* buf, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                     uint32_t color) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > buf->width) x1 = buf->width;
    if (y1 > buf->height) y1 = buf->height;
    uint32_t* base = (uint32_t*)buf->bits;
    for (int32_t y = y0; y < y1; y++) {
        uint32_t* row = base + (size_t)y * (size_t)buf->stride;
        for (int32_t x = x0; x < x1; x++) {
            row[x] = color;
        }
    }
}

static void OutlineRect(const ANativeWindow_Buffer* buf, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                        int32_t thickness, uint32_t color) {
    FillRect(buf, x0, y0, x1, y0 + thickness, color);
    FillRect(buf, x0, y1 - thickness, x1, y1, color);
    FillRect(buf, x0, y0, x0 + thickness, y1, color);
    FillRect(buf, x1 - thickness, y0, x1, y1, color);
}

/* Channel-wise mix of two panel colors, t/256 toward b — used to derive
 * schematic/plate tones from the decoded window palette instead of
 * inventing new hues. */
static uint32_t MixColor(uint32_t a, uint32_t b, uint32_t t) {
    uint32_t r = ((a & 0xFF) * (256 - t) + (b & 0xFF) * t) >> 8;
    uint32_t g = (((a >> 8) & 0xFF) * (256 - t) + ((b >> 8) & 0xFF) * t) >> 8;
    uint32_t bl = (((a >> 16) & 0xFF) * (256 - t) + ((b >> 16) & 0xFF) * t) >> 8;
    return RGB(r, g, bl);
}

/* Nearest-neighbor integer-scale blit of a cached theme sprite; alpha-0
 * source pixels are skipped so sprites sit on whatever is behind them. */
static void BlitSprite(const ANativeWindow_Buffer* buf, const SecondScreenThemeSprite* spr, int32_t x,
                       int32_t y, int32_t scale) {
    if (spr == NULL || spr->px == NULL || scale < 1) {
        return;
    }
    uint32_t* base = (uint32_t*)buf->bits;
    for (int32_t sy = 0; sy < spr->h; sy++) {
        for (int32_t sx = 0; sx < spr->w; sx++) {
            uint32_t c = spr->px[sy * spr->w + sx];
            if (c == 0) {
                continue;
            }
            int32_t dy0 = y + sy * scale;
            for (int32_t ey = 0; ey < scale; ey++) {
                int32_t dy = dy0 + ey;
                if (dy < 0 || dy >= buf->height) {
                    continue;
                }
                uint32_t* row = base + (size_t)dy * (size_t)buf->stride;
                int32_t dx0 = x + sx * scale;
                for (int32_t ex = 0; ex < scale; ex++) {
                    int32_t dx = dx0 + ex;
                    if (dx >= 0 && dx < buf->width) {
                        row[dx] = c;
                    }
                }
            }
        }
    }
}

/* 3x5 bitmap glyphs, 15 bits row-major from the top-left (bit 14 first).
 * Fallback-only: everything normally uses the decoded HUD font; these
 * appear just in the pre-ROM window or if a decode failed. */
static uint16_t GlyphBits(char c) {
    switch (c) {
        case '0': return 0x7B6F; /* 111 101 101 101 111 */
        case '1': return 0x2C97; /* 010 110 010 010 111 */
        case '2': return 0x73E7; /* 111 001 111 100 111 */
        case '3': return 0x73CF; /* 111 001 111 001 111 */
        case '4': return 0x5BC9; /* 101 101 111 001 001 */
        case '5': return 0x79CF; /* 111 100 111 001 111 */
        case '6': return 0x79EF; /* 111 100 111 101 111 */
        case '7': return 0x7292; /* 111 001 010 010 010 */
        case '8': return 0x7BEF; /* 111 101 111 101 111 */
        case '9': return 0x7BCF; /* 111 101 111 001 111 */
        case 'A': return 0x2BED; /* 010 101 111 101 101 */
        case 'B': return 0x6BAE; /* 110 101 110 101 110 */
        default: return 0;
    }
}

static void DrawGlyph(const ANativeWindow_Buffer* buf, int32_t x, int32_t y, int32_t scale, char c,
                      uint32_t color) {
    uint16_t bits = GlyphBits(c);
    for (int32_t py = 0; py < 5; py++) {
        for (int32_t px = 0; px < 3; px++) {
            if (bits & (1u << (14 - (py * 3 + px)))) {
                FillRect(buf, x + px * scale, y + py * scale, x + (px + 1) * scale, y + (py + 1) * scale,
                         color);
            }
        }
    }
}

/* Fallback glyph with the dark outline light glyphs need to sit on light
 * panels (drawn as 4-neighbor dark copies underneath). */
static void DrawOutlinedGlyph(const ANativeWindow_Buffer* buf, int32_t x, int32_t y, int32_t scale, char c,
                              uint32_t color, uint32_t outline) {
    DrawGlyph(buf, x - scale, y, scale, c, outline);
    DrawGlyph(buf, x + scale, y, scale, c, outline);
    DrawGlyph(buf, x, y - scale, scale, c, outline);
    DrawGlyph(buf, x, y + scale, scale, c, outline);
    DrawGlyph(buf, x, y, scale, c, color);
}

/* Right-aligned counter in the HUD's own 8x16 digit font (yellow variant
 * for maxed counters, exactly like RenderDigits). Returns the left edge
 * reached so callers can place an icon in front. minDigits pads with
 * leading zeros the way the HUD pads the rupee counter. */
static int32_t DrawHudNumber(const ANativeWindow_Buffer* buf, int32_t xRight, int32_t y, int32_t scale,
                             uint32_t value, int32_t minDigits, int yellow) {
    int32_t x = xRight;
    int32_t drawn = 0;
    do {
        const SecondScreenThemeSprite* d =
            Port_SecondScreenTheme_Get((yellow ? SST_DIGIT_YELLOW_0 : SST_DIGIT_WHITE_0) + (int)(value % 10));
        x -= 8 * scale;
        if (d != NULL) {
            BlitSprite(buf, d, x, y, scale);
        } else {
            DrawOutlinedGlyph(buf, x + 2 * scale, y + 3 * scale, scale * 2, (char)('0' + value % 10),
                              Port_SecondScreenTheme_Color(SSC_TEXT_LIGHT),
                              Port_SecondScreenTheme_Color(SSC_BORDER_DARK));
        }
        value /= 10;
        drawn++;
    } while (value != 0 || drawn < minDigits);
    return x;
}

/* Two-glyph ammo count in the HUD's small 8x8 font (tens glyph is
 * right-aligned in its tile, ones left-aligned — sub_0801C2F0's pair). */
static void DrawAmmoCount(const ANativeWindow_Buffer* buf, int32_t x, int32_t y, int32_t scale,
                          uint32_t value) {
    if (value > 99) {
        value = 99;
    }
    const SecondScreenThemeSprite* tens = Port_SecondScreenTheme_Get(SST_SMALL_TENS_0 + (int)(value / 10));
    const SecondScreenThemeSprite* ones = Port_SecondScreenTheme_Get(SST_SMALL_ONES_0 + (int)(value % 10));
    if (tens == NULL || ones == NULL) {
        return; /* purely decorative — skip rather than substitute */
    }
    BlitSprite(buf, tens, x, y, scale);
    BlitSprite(buf, ones, x + 8 * scale, y, scale);
}

/* Chunky filled diamond (the four-element motif); r is the half-height. */
static void FillDiamond(const ANativeWindow_Buffer* buf, int32_t cx, int32_t cy, int32_t r, uint32_t color) {
    for (int32_t i = -r; i <= r; i++) {
        int32_t half = r - (i < 0 ? -i : i);
        FillRect(buf, cx - half, cy + i, cx + half + 1, cy + i + 1, color);
    }
}

/* ------------------------------------------------------------------ */
/*  Idle screen                                                        */
/* ------------------------------------------------------------------ */

/* Title screen / file select / cutscenes: nothing to mirror, so go quiet —
 * near-black with the four-element diamond breathing on a ~2.4 s sine.
 * Deliberately minimal: decorated idle cards were rejected on zelda3. */
static void PaintIdle(const ANativeWindow_Buffer* buf, uint32_t tick) {
    FillRect(buf, 0, 0, buf->width, buf->height, COL_IDLE_BG);

    /* 2.4 s period at the 20 Hz paint rate = 48 ticks. */
    float pulse = 0.60f + 0.28f * sinf((float)(tick % 48) * (6.28318f / 48.0f));
    int32_t cx = buf->width / 2;
    int32_t cy = buf->height / 2;
    int32_t r = (buf->width < buf->height ? buf->width : buf->height) / 26;
    int32_t d = r * 2 + r / 2;
    static const uint32_t kElem[4] = { COL_ELEM_WIND, COL_ELEM_FIRE, COL_ELEM_EARTH, COL_ELEM_WATER };
    static const int32_t kOff[4][2] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };
    for (int e = 0; e < 4; e++) {
        uint32_t c = kElem[e];
        uint32_t rr = (uint32_t)((c & 0xFF) * pulse);
        uint32_t gg = (uint32_t)(((c >> 8) & 0xFF) * pulse);
        uint32_t bb = (uint32_t)(((c >> 16) & 0xFF) * pulse);
        FillDiamond(buf, cx + kOff[e][0] * d, cy + kOff[e][1] * d, r, RGB(rr, gg, bb));
    }
}

/* ------------------------------------------------------------------ */
/*  Map panel                                                          */
/* ------------------------------------------------------------------ */

static void DrawMapMarker(const ANativeWindow_Buffer* buf, int32_t px, int32_t py, int32_t unit,
                          uint32_t color) {
    FillDiamond(buf, px, py, unit + 1, Port_SecondScreenTheme_Color(SSC_BORDER_DARK));
    FillDiamond(buf, px, py, unit, color);
}

/* Schematic area map — the styled fallback whenever the authentic art
 * modules report "not ready". Rooms placed by their real in-area geometry
 * (RoomResInfo), toned from the decoded window palette so it reads as part
 * of the theme, not programmer art. In dungeons it obeys the same reveal
 * rule as the real map screen: unvisited rooms only appear once the
 * dungeon map item is owned (dungeonItemBits bit 0 — HasDungeonMap);
 * visited rooms always do. */
static void PaintSchematic(const ANativeWindow_Buffer* buf, const SecondScreenSnapshot* snap, int32_t x0,
                           int32_t y0, int32_t x1, int32_t y1, uint32_t tick, int32_t unit) {
    uint32_t fill = Port_SecondScreenTheme_Color(SSC_WINDOW_FILL);
    uint32_t light = Port_SecondScreenTheme_Color(SSC_BORDER_LIGHT);
    uint32_t dark = Port_SecondScreenTheme_Color(SSC_BORDER_DARK);
    uint32_t colSeen = MixColor(fill, light, 96);
    uint32_t colUnseen = MixColor(fill, dark, 96);
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
        /* seam inset keeps a dark gap between adjacent rooms so the layout
         * reads as separate rooms, not one blob. */
        FillRect(buf, rx0, ry0, rx1, ry1, dark);
        FillRect(buf, rx0 + seam, ry0 + seam, rx1 - seam, ry1 - seam, roomFill);
    }

    int32_t px = ox + (int32_t)((snap->playerX - minX) * scale);
    int32_t py = oy + (int32_t)((snap->playerY - minY) * scale);
    DrawMapMarker(buf, px, py, unit,
                  (tick & 8) ? RGB(0xF8, 0xF8, 0xF8) : Port_SecondScreenTheme_Color(SSC_GOLD));
}

/* Floor pips beside an authentic dungeon map, topmost floor first —
 * mirrors the floor list of the real dungeon-map screen. */
static void DrawFloorPips(const ANativeWindow_Buffer* buf, int32_t x, int32_t y0, int32_t y1, int32_t unit,
                          int floorCount, int currentFloor) {
    int32_t pipH = unit * 4;
    int32_t pipW = unit * 6;
    int32_t gap = unit;
    int32_t totalH = floorCount * pipH + (floorCount - 1) * gap;
    int32_t y = y0 + ((y1 - y0) - totalH) / 2;
    for (int i = 0; i < floorCount; i++) {
        uint32_t fill = (i == currentFloor)
                            ? Port_SecondScreenTheme_Color(SSC_GOLD)
                            : MixColor(Port_SecondScreenTheme_Color(SSC_WINDOW_FILL),
                                       Port_SecondScreenTheme_Color(SSC_BORDER_DARK), 80);
        FillRect(buf, x, y, x + pipW, y + pipH, Port_SecondScreenTheme_Color(SSC_BORDER_DARK));
        FillRect(buf, x + 1, y + 1, x + pipW - 1, y + pipH - 1, fill);
        y += pipH + gap;
    }
}

/* Left panel: the area map inside TMC menu chrome. Authentic dungeon
 * floors / world-map artwork when the sibling modules have decoded them,
 * the styled schematic otherwise; the world map keeps a frozen fix while
 * Link is indoors. */
static void PaintMapPanel(const ANativeWindow_Buffer* buf, const SecondScreenSnapshot* snap, int32_t x0,
                          int32_t y0, int32_t x1, int32_t y1, uint32_t tick, int32_t ts) {
    Port_SecondScreenTheme_DrawWindow((uint32_t*)buf->bits, buf->width, buf->height, buf->stride, x0, y0,
                                      x1 - x0, y1 - y0, ts);
    int32_t inset = 8 * ts + ts;
    int32_t ix0 = x0 + inset, iy0 = y0 + inset, ix1 = x1 - inset, iy1 = y1 - inset;
    if (ix1 - ix0 < 16 || iy1 - iy0 < 16) {
        return;
    }
    int32_t unit = ts;

    if (snap->areaFlags & SECOND_SCREEN_AR_IS_DUNGEON) {
        SecondScreenDungeonMapInfo info;
        if (Port_SecondScreenDungeonMap_GetInfo(snap->dungeonIdx, snap->area, snap->room, &info) &&
            info.floorCount > 0) {
            int32_t gutter = info.floorCount > 1 ? 9 * ts : 0;
            if (Port_SecondScreenDungeonMap_Draw((uint32_t*)buf->bits, buf->width, buf->height, buf->stride,
                                                 ix0 + gutter, iy0, (ix1 - ix0) - gutter, iy1 - iy0,
                                                 snap->dungeonIdx, info.currentFloor, snap->area, snap->room,
                                                 snap->visitedMask, snap->dungeonItemBits, snap->playerX,
                                                 snap->playerY, tick)) {
                if (gutter != 0) {
                    DrawFloorPips(buf, ix0, iy0, iy1, unit, info.floorCount, info.currentFloor);
                }
                return;
            }
        }
        PaintSchematic(buf, snap, ix0, iy0, ix1, iy1, tick, unit);
        return;
    }

    /* Outdoors (and indoors-but-not-dungeon, which reuses the frozen
     * outdoor fix): the whole-Hyrule map. */
    int32_t imgW = 0, imgH = 0;
    const uint32_t* img = Port_SecondScreenWorldMap_GetImage(&imgW, &imgH);
    if (img != NULL && imgW > 0 && imgH > 0) {
        int32_t mx, my;
        if (Port_SecondScreenWorldMap_LocatePlayer(snap->area, snap->playerX, snap->playerY, &mx, &my)) {
            sLastFix.valid = 1;
            sLastFix.mapX = mx;
            sLastFix.mapY = my;
        }

        /* Fit whole-map: integer nearest-neighbor scale when that fills a
         * reasonable share of the panel, fractional nearest fit otherwise
         * (hard pixel edges either way — never smoothed). */
        int32_t iw = ix1 - ix0, ih = iy1 - iy0;
        int32_t dw, dh;
        int32_t s = iw / imgW < ih / imgH ? iw / imgW : ih / imgH;
        if (s >= 1 && (imgW * s * 10 >= iw * 6 || imgH * s * 10 >= ih * 6)) {
            dw = imgW * s;
            dh = imgH * s;
        } else if ((int64_t)iw * imgH <= (int64_t)ih * imgW) {
            dw = iw;
            dh = (int32_t)((int64_t)imgH * iw / imgW);
        } else {
            dh = ih;
            dw = (int32_t)((int64_t)imgW * ih / imgH);
        }
        int32_t dx0 = ix0 + (iw - dw) / 2;
        int32_t dy0 = iy0 + (ih - dh) / 2;
        uint32_t* base = (uint32_t*)buf->bits;
        for (int32_t dy = 0; dy < dh; dy++) {
            int32_t ty = dy0 + dy;
            if (ty < 0 || ty >= buf->height) {
                continue;
            }
            const uint32_t* srcRow = img + (size_t)((int64_t)dy * imgH / dh) * (size_t)imgW;
            uint32_t* dstRow = base + (size_t)ty * (size_t)buf->stride;
            for (int32_t dx = 0; dx < dw; dx++) {
                int32_t tx = dx0 + dx;
                if (tx >= 0 && tx < buf->width) {
                    dstRow[tx] = srcRow[(int64_t)dx * imgW / dw];
                }
            }
        }

        /* Windcrest pins for the crests unlocked in the save (bits 24..31
         * of gSave.windcrests are the WindcrestID flags). */
        for (int32_t id = 24; id < 32; id++) {
            int32_t wx, wy;
            if (((snap->windcrests >> id) & 1u) && Port_SecondScreenWorldMap_GetWindcrestPin(id, &wx, &wy)) {
                DrawMapMarker(buf, dx0 + wx * dw / imgW, dy0 + wy * dh / imgH, unit,
                              Port_SecondScreenTheme_Color(SSC_RUPEE_GREEN));
            }
        }

        /* Player marker: live outdoors it blinks like the game's own map
         * dot; indoors the frozen fix holds steady (reads as "last seen
         * here", the doorway marker). */
        if (sLastFix.valid) {
            int32_t px = dx0 + sLastFix.mapX * dw / imgW;
            int32_t py = dy0 + sLastFix.mapY * dh / imgH;
            uint32_t gold = Port_SecondScreenTheme_Color(SSC_GOLD);
            uint32_t c = (snap->areaFlags & SECOND_SCREEN_AR_IS_OVERWORLD)
                             ? ((tick & 8) ? RGB(0xF8, 0xF8, 0xF8) : gold)
                             : gold;
            DrawMapMarker(buf, px, py, unit + 1, c);
        }
        return;
    }

    PaintSchematic(buf, snap, ix0, iy0, ix1, iy1, tick, unit);
}

/* ------------------------------------------------------------------ */
/*  Item grid                                                          */
/* ------------------------------------------------------------------ */

/* Right panel: the pause menu's 16 equip slots as a 4x4 touch grid (same
 * slot order as the real Items screen — three rows of gear, the bottles
 * along the bottom), dressed in the menu's own chrome: window frame,
 * plate cells, real item icons, the real blinking gold equip cursor and
 * the HUD's A/B button bubbles on the equipped slots. Rebuilds
 * sTapTargets to match wherever the cells actually landed this frame. */
static void PaintItems(const ANativeWindow_Buffer* buf, const SecondScreenSnapshot* snap, int32_t x0,
                       int32_t y0, int32_t x1, int32_t y1, uint32_t tick, int32_t ts) {
    Port_SecondScreenTheme_DrawWindow((uint32_t*)buf->bits, buf->width, buf->height, buf->stride, x0, y0,
                                      x1 - x0, y1 - y0, ts);
    int32_t inset = 8 * ts + ts;
    int32_t ix0 = x0 + inset, iy0 = y0 + inset, ix1 = x1 - inset, iy1 = y1 - inset;

    const int cols = 4, rows = 4;
    int32_t cellW = (ix1 - ix0) / cols;
    int32_t cellH = (iy1 - iy0) / rows;
    int32_t cell = cellW < cellH ? cellW : cellH;
    if (cell < 24) {
        return;
    }
    /* Center the square grid in the interior. */
    int32_t gx0 = ix0 + ((ix1 - ix0) - cell * cols) / 2;
    int32_t gy0 = iy0 + ((iy1 - iy0) - cell * rows) / 2;
    int32_t gap = cell / 10;
    int32_t seam = ts > 2 ? ts / 2 : 1;

    uint32_t fill = Port_SecondScreenTheme_Color(SSC_WINDOW_FILL);
    uint32_t dark = Port_SecondScreenTheme_Color(SSC_BORDER_DARK);
    uint32_t plate = MixColor(fill, dark, 64);
    uint32_t plateEmpty = MixColor(fill, dark, 32);

    const SecondScreenThemeSprite* cursor =
        Port_SecondScreenTheme_Get((tick & 8) ? SST_CURSOR_1 : SST_CURSOR_0);

    TapTarget targets[SECOND_SCREEN_ITEM_SLOTS];
    int targetCount = 0;

    for (int slot = 0; slot < SECOND_SCREEN_ITEM_SLOTS; slot++) {
        int32_t cx0 = gx0 + (slot % cols) * cell + gap;
        int32_t cy0 = gy0 + (slot / cols) * cell + gap;
        int32_t cx1 = cx0 + cell - 2 * gap;
        int32_t cy1 = cy0 + cell - 2 * gap;

        uint8_t itemId = snap->menuItems[slot];

        /* Plate: an inset well in the window's own tones. */
        FillRect(buf, cx0, cy0, cx1, cy1, dark);
        FillRect(buf, cx0 + seam, cy0 + seam, cx1 - seam, cy1 - seam, itemId ? plate : plateEmpty);
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
        int32_t iconX = (cx0 + cx1) / 2 - 8 * iconScale;
        int32_t iconY = (cy0 + cy1) / 2 - 8 * iconScale;
        Port_SecondScreenRender_DrawItemIcon((uint32_t*)buf->bits, buf->width, buf->height, buf->stride,
                                             iconX, iconY, iconScale, iconId);

        /* Ammo under bombs/bow — the HUD's own tiny digit pair. */
        if (itemId == ITEMID_BOMBS || itemId == ITEMID_REMOTE_BOMBS) {
            int32_t ss = (iconScale + 1) / 2;
            DrawAmmoCount(buf, iconX, cy1 - seam - 8 * ss, ss, snap->bombCount);
        } else if (itemId == ITEMID_BOW || itemId == ITEMID_LIGHT_ARROW) {
            int32_t ss = (iconScale + 1) / 2;
            DrawAmmoCount(buf, iconX, cy1 - seam - 8 * ss, ss, snap->arrowCount);
        }

        if (slot == snap->equippedSlotA || slot == snap->equippedSlotB) {
            bool isA = slot == snap->equippedSlotA;
            if (cursor != NULL) {
                /* The Items screen's blinking gold slot frame, centered
                 * over the cell (it overhangs a little, as in the menu). */
                int32_t cmax = cursor->w > cursor->h ? cursor->w : cursor->h;
                int32_t cs = (cell + cell / 4) / cmax;
                if (cs < 1) cs = 1;
                BlitSprite(buf, cursor, (cx0 + cx1) / 2 - cursor->w * cs / 2,
                           (cy0 + cy1) / 2 - cursor->h * cs / 2, cs);
            } else {
                OutlineRect(buf, cx0, cy0, cx1, cy1, seam * 2, Port_SecondScreenTheme_Color(SSC_GOLD));
            }
            const SecondScreenThemeSprite* badge =
                Port_SecondScreenTheme_Get(isA ? SST_BUTTON_A : SST_BUTTON_B);
            if (badge != NULL) {
                int32_t bmax = badge->w > badge->h ? badge->w : badge->h;
                int32_t bs = (cell / 3) / bmax;
                if (bs < 1) bs = 1;
                int32_t bx = isA ? cx0 - seam : cx1 + seam - badge->w * bs;
                BlitSprite(buf, badge, bx, cy0 - seam, bs);
            } else {
                int32_t tagS = cell / 28 > 0 ? cell / 28 : 1;
                int32_t tagX = isA ? cx0 : cx1 - 5 * tagS;
                FillRect(buf, tagX, cy0, tagX + 5 * tagS, cy0 + 7 * tagS,
                         Port_SecondScreenTheme_Color(SSC_BORDER_DARK));
                DrawGlyph(buf, tagX + tagS, cy0 + tagS, tagS, isA ? 'A' : 'B',
                          Port_SecondScreenTheme_Color(SSC_TEXT_LIGHT));
            }
        }

        targets[targetCount++] = (TapTarget) { cx0, cy0, cx1, cy1, itemId };
    }

    pthread_mutex_lock(&sTapTargetMutex);
    for (int i = 0; i < targetCount; i++) {
        sTapTargets[i] = targets[i];
    }
    sTapTargetCount = targetCount;
    pthread_mutex_unlock(&sTapTargetMutex);
}

/* ------------------------------------------------------------------ */
/*  Status strip                                                       */
/* ------------------------------------------------------------------ */

/* Bottom strip in HUD language: hearts (up to TMC's max of 20 — two rows
 * of 10, quarter-heart states), rupees with the wallet's own icon, and
 * either the dungeon set (keys + map/compass/big-key pips) or the quest
 * set (element diamonds + kinstone chip). Floats on the backdrop like
 * the real HUD floats on gameplay — no window chrome down here. */
static void PaintStatus(const ANativeWindow_Buffer* buf, const SecondScreenSnapshot* snap, int32_t x0,
                        int32_t y0, int32_t x1, int32_t y1) {
    int isDungeon = (snap->areaFlags & SECOND_SCREEN_AR_IS_DUNGEON) != 0;
    int hasKeys = (snap->areaFlags & SECOND_SCREEN_AR_HAS_KEYS) != 0;

    int maxHearts = snap->maxHealth / 8;
    if (maxHearts > 20) maxHearts = 20;
    if (maxHearts < 1) maxHearts = 1;
    int heartCols = maxHearts > 10 ? 10 : maxHearts;

    /* Everything is laid out in native GBA pixels first, then drawn at
     * the largest integer scale that fits both strip axes. */
    int32_t nativeW = heartCols * 8 + 10 + 16 + 2 + 3 * 8; /* hearts + rupee block */
    if (isDungeon || hasKeys) {
        nativeW += 10 + 16 + 2 + 2 * 8 + 8 + 3 * 10; /* keys + three pips */
    } else {
        nativeW += 10 + 4 * 7 + 8 + 16 + 2 + 3 * 8; /* elements + kinstone chip */
    }
    int32_t ks = (x1 - x0) / nativeW;
    int32_t ksH = (y1 - y0) / 18; /* two heart rows + breathing room */
    if (ksH < ks) ks = ksH;
    if (ks < 1) ks = 1;

    int32_t x = x0;
    int32_t midY = (y0 + y1) / 2;

    /* Hearts: quarter-heart states exactly like DrawHearts (health is in
     * eighths in the save; the HUD shows quarters, 1 unit shows 1/4). */
    {
        int quarters = snap->health == 1 ? 1 : snap->health / 2;
        int fullHearts = quarters / 4;
        int frac = quarters & 3;
        int rows = maxHearts > 10 ? 2 : 1;
        int32_t hy = midY - rows * 4 * ks;
        for (int i = 0; i < maxHearts; i++) {
            int state;
            if (i < fullHearts) {
                state = SST_HEART_FULL;
            } else if (i == fullHearts && frac != 0) {
                state = SST_HEART_Q1 + (frac - 1);
            } else {
                state = SST_HEART_EMPTY;
            }
            const SecondScreenThemeSprite* h = Port_SecondScreenTheme_Get(state);
            int32_t hx = x + (i % 10) * 8 * ks;
            int32_t hyy = hy + (i / 10) * 8 * ks;
            if (h != NULL) {
                BlitSprite(buf, h, hx, hyy, ks);
            } else {
                uint32_t c = state == SST_HEART_EMPTY ? RGB(0x38, 0x20, 0x20)
                                                      : Port_SecondScreenTheme_Color(SSC_HEART_RED);
                FillRect(buf, hx + ks, hyy + ks, hx + 7 * ks, hyy + 7 * ks, c);
            }
        }
        x += heartCols * 8 * ks + 10 * ks;
    }

    /* Rupees: the wallet's icon + three HUD digits (yellow when the
     * wallet is full, RenderDigits' own convention). */
    {
        BlitSprite(buf, Port_SecondScreenTheme_Get(SST_RUPEE_WALLET0 + (snap->walletType & 3)), x,
                   midY - 8 * ks, ks);
        DrawHudNumber(buf, x + (16 + 2 + 3 * 8) * ks, midY - 8 * ks, ks, snap->rupees, 3,
                      snap->walletMax != 0 && snap->rupees >= snap->walletMax);
        x += (16 + 2 + 3 * 8) * ks + 10 * ks;
    }

    if (isDungeon || hasKeys) {
        /* Small keys + dungeon inventory pips. Possession display only —
         * bit 0 map, bit 1 compass, bit 2 big key, the exact
         * HasDungeonMap/Compass/BigKey bits (src/gameUtils.c); reveal
         * gating on the map itself stays in the dungeon-map module. */
        BlitSprite(buf, Port_SecondScreenTheme_Get(SST_KEY), x, midY - 8 * ks, ks);
        DrawHudNumber(buf, x + (16 + 2 + 2 * 8) * ks, midY - 8 * ks, ks, snap->dungeonKeys, 2, 0);
        x += (16 + 2 + 2 * 8) * ks + 8 * ks;

        static const uint8_t kPipItems[3] = { ITEMID_DUNGEON_MAP, ITEMID_COMPASS, ITEMID_BIG_KEY };
        static const uint8_t kPipBits[3] = { 1, 2, 4 };
        int32_t pipScale = ks > 1 ? ks - ks / 2 : 1; /* 16px icons at ~half the HUD scale */
        int32_t pipPitch = 16 * pipScale + 2 + 2 * ks;
        for (int i = 0; i < 3; i++) {
            int32_t px = x + i * pipPitch;
            uint32_t well = MixColor(COL_BG, Port_SecondScreenTheme_Color(SSC_BORDER_LIGHT), 40);
            FillRect(buf, px, midY - 8 * pipScale - 1, px + 16 * pipScale + 2, midY + 8 * pipScale + 1,
                     well);
            if (snap->dungeonItemBits & kPipBits[i]) {
                Port_SecondScreenRender_DrawItemIcon((uint32_t*)buf->bits, buf->width, buf->height,
                                                     buf->stride, px + 1, midY - 8 * pipScale, pipScale,
                                                     kPipItems[i]);
            }
        }
    } else {
        /* Elements owned, in the idle emblem's language, then the
         * kinstone chip: bag count big, fused count small beside it. */
        static const uint32_t kElemCols[4] = { COL_ELEM_EARTH, COL_ELEM_FIRE, COL_ELEM_WATER,
                                               COL_ELEM_WIND };
        int32_t r = 3 * ks;
        for (int i = 0; i < 4; i++) {
            int32_t ex = x + i * 7 * ks + r;
            uint32_t c = ((snap->elements >> i) & 1)
                             ? kElemCols[i]
                             : MixColor(COL_BG, Port_SecondScreenTheme_Color(SSC_BORDER_LIGHT), 48);
            FillDiamond(buf, ex, midY, r, Port_SecondScreenTheme_Color(SSC_BORDER_DARK));
            FillDiamond(buf, ex, midY, r - 1, c);
        }
        x += 4 * 7 * ks + 8 * ks;

        if (x + (16 + 2 + 3 * 8) * ks <= x1) {
            uint32_t bag = snap->kinstoneBag > 999 ? 999 : snap->kinstoneBag; /* keep to the 3-digit block */
            Port_SecondScreenRender_DrawItemIcon((uint32_t*)buf->bits, buf->width, buf->height, buf->stride,
                                                 x, midY - 8 * ks, ks, ITEMID_KINSTONE);
            DrawHudNumber(buf, x + (16 + 2 + 3 * 8) * ks, midY - 8 * ks, ks, bag, 1, 0);
            DrawAmmoCount(buf, x + (16 + 2 + 3 * 8) * ks + 2 * ks, midY + 2 * ks, ks > 1 ? ks / 2 : 1,
                          snap->kinstoneFused);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Frame composition                                                  */
/* ------------------------------------------------------------------ */

/* Dual-screen layout in the sibling zelda3-android mod's arrangement,
 * dressed as TMC: the area/world map in a menu window on the left, the
 * touch inventory in a menu window on the right, a HUD status strip along
 * the bottom. All sizes derive from the surface (dev 1280x720, Thor
 * bottom panel 1080x1240) so the roughly-square panel reads as designed,
 * not stretched. */
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

    if (!snap.inGame) {
        pthread_mutex_lock(&sTapTargetMutex);
        sTapTargetCount = 0; /* no gameplay, no equip targets */
        pthread_mutex_unlock(&sTapTargetMutex);
        sLastFix.valid = 0; /* stale fixes must not survive into a new save */
        PaintIdle(&buf, sTick);
        ANativeWindow_unlockAndPost(window);
        return;
    }

    /* Theme decode happens lazily here — only during gameplay, when the
     * ROM tables are guaranteed resolved. Until it reports ready (the
     * first gameplay frame in practice) everything below still renders
     * through its per-element fallbacks. */
    Port_SecondScreenTheme_Ready();

    FillRect(&buf, 0, 0, buf.width, buf.height, COL_BG);

    /* One scale unit for the whole layout: how many device pixels one GBA
     * pixel gets. min-axis/240 keeps the chrome in proportion on both the
     * wide dev surface and the Thor's near-square bottom panel. */
    int32_t minAxis = buf.width < buf.height ? buf.width : buf.height;
    int32_t ts = minAxis / 240;
    if (ts < 2) ts = 2;
    if (ts > 6) ts = 6;

    int32_t pad = ts * 3;
    int32_t statusH = 20 * ts + 2 * pad;
    /* The gesture zone eats touches at the very bottom edge; the (non-
     * interactive) status strip absorbs that region, keeping the tappable
     * item grid well clear of it. */
    int32_t panelBottom = buf.height - statusH - pad;
    int32_t split = buf.width * 47 / 100; /* map | items */

    PaintMapPanel(&buf, &snap, pad, pad, split - pad / 2, panelBottom, sTick, ts);
    PaintItems(&buf, &snap, split + pad / 2, pad, buf.width - pad, panelBottom, sTick, ts);
    PaintStatus(&buf, &snap, pad * 2, panelBottom + pad, buf.width - pad * 2, buf.height - pad);

    ANativeWindow_unlockAndPost(window);
}

void Port_SecondScreen_OnTap(int x, int y, int longPress) {
    uint8_t itemId = 0;
    pthread_mutex_lock(&sTapTargetMutex);
    for (int i = 0; i < sTapTargetCount; i++) {
        const TapTarget* t = &sTapTargets[i];
        if (x >= t->x0 && x < t->x1 && y >= t->y0 && y < t->y1) {
            itemId = t->itemId;
            break;
        }
    }
    pthread_mutex_unlock(&sTapTargetMutex);

    if (itemId != 0) {
        Port_SecondScreenState_RequestEquip(itemId, longPress ? 1 : 0);
    }
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

#else /* !__ANDROID__ — no second display; every entry point is a no-op. */

void Port_SecondScreen_Init(void) {}
void Port_SecondScreen_OnSurfaceReady(void* window, int width, int height) {
    (void)window;
    (void)width;
    (void)height;
}
void Port_SecondScreen_OnSurfaceLost(void) {}
void Port_SecondScreen_OnTap(int x, int y, int longPress) {
    (void)x;
    (void)y;
    (void)longPress;
}

#endif
