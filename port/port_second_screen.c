#include "port_second_screen.h"
#include "port_second_screen_render.h"
#include "port_second_screen_state.h"

#include <stdio.h>

#ifdef __ANDROID__

#include <android/native_window.h>
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

#define COL_BG RGB(0x10, 0x14, 0x10)
#define COL_PANEL RGB(0x18, 0x20, 0x18)
#define COL_ROOM_UNSEEN RGB(0x24, 0x2A, 0x30)
#define COL_ROOM_SEEN RGB(0x3E, 0x6E, 0xA8)
#define COL_ROOM_HERE RGB(0x6A, 0xA6, 0xE8)
#define COL_ROOM_EDGE RGB(0x10, 0x14, 0x18)
#define COL_CELL_EMPTY RGB(0x20, 0x20, 0x20)
#define COL_CELL_OWNED RGB(0x28, 0x30, 0x40)
#define COL_EQUIP_BORDER RGB(0xE8, 0xC8, 0x40) /* gold, like the pause menu cursor */
#define COL_TAG_A RGB(0x40, 0xC8, 0x58)
#define COL_TAG_B RGB(0x58, 0x80, 0xF0)
#define COL_HEART_FULL RGB(0xE8, 0x30, 0x30)
#define COL_HEART_EMPTY RGB(0x38, 0x20, 0x20)
#define COL_RUPEE RGB(0x48, 0xC8, 0x58)
#define COL_TEXT RGB(0xE8, 0xE8, 0xE8)
#define COL_TRIFORCE RGB(0xE8, 0xC8, 0x40)

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

/* 3x5 bitmap glyphs, 15 bits row-major from the top-left (bit 14 first).
 * Just digits plus the A/B equip tags — everything else on the panel is
 * iconography, so a full font stays out of scope. */
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

/* Right-aligned unsigned number ending just before (xRight, y). Returns the
 * left edge it reached, so callers can place an icon in front of it. */
static int32_t DrawNumber(const ANativeWindow_Buffer* buf, int32_t xRight, int32_t y, int32_t scale,
                           uint32_t value, uint32_t color) {
    int32_t x = xRight;
    do {
        x -= 4 * scale;
        DrawGlyph(buf, x, y, scale, (char)('0' + value % 10), color);
        value /= 10;
    } while (value != 0);
    return x;
}

/* Upward-pointing filled triangle, apex at (cx, top), rows widen by one
 * pixel per side per row — chunky on purpose, matching the pixel look. */
static void FillTriangle(const ANativeWindow_Buffer* buf, int32_t cx, int32_t top, int32_t height,
                          uint32_t color) {
    for (int32_t i = 0; i < height; i++) {
        FillRect(buf, cx - i, top + i, cx + i + 1, top + i + 1, color);
    }
}

/* Title screens, cutscenes, file select: nothing useful to mirror, so show
 * the classic idle mark instead of stale gameplay data — same choice
 * zelda3-android makes for its second screen. */
static void PaintIdle(const ANativeWindow_Buffer* buf) {
    int32_t h = buf->height / 4;
    int32_t cx = buf->width / 2;
    int32_t top = (buf->height - 2 * h) / 2;
    FillTriangle(buf, cx, top, h, COL_TRIFORCE);
    FillTriangle(buf, cx - h, top + h, h, COL_TRIFORCE);
    FillTriangle(buf, cx + h, top + h, h, COL_TRIFORCE);
}

/* Automap panel: every room of the current area placed by its real in-area
 * geometry (RoomResInfo.map_x/y + pixel size), visited rooms lit, current
 * room bright, player as a pulsing dot at their true area-space position.
 * Works for dungeons and overworld areas alike; swapping the schematic
 * fill for extracted world-map artwork is a later, purely-visual upgrade
 * that keeps this exact placement math. */
static void PaintMap(const ANativeWindow_Buffer* buf, const SecondScreenSnapshot* snap, int32_t x0,
                      int32_t y0, int32_t x1, int32_t y1, uint32_t tick) {
    FillRect(buf, x0, y0, x1, y1, COL_PANEL);

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

    int32_t pad = (y1 - y0) / 20;
    float scaleX = (float)(x1 - x0 - 2 * pad) / (float)(maxX - minX);
    float scaleY = (float)(y1 - y0 - 2 * pad) / (float)(maxY - minY);
    float scale = scaleX < scaleY ? scaleX : scaleY;
    /* Center the fitted map inside the panel. */
    int32_t ox = x0 + (int32_t)((x1 - x0) - (maxX - minX) * scale) / 2;
    int32_t oy = y0 + (int32_t)((y1 - y0) - (maxY - minY) * scale) / 2;

    for (int i = 0; i < SECOND_SCREEN_MAX_ROOMS; i++) {
        const SecondScreenRoom* r = &snap->rooms[i];
        if (r->w == 0 || r->h == 0) {
            continue;
        }
        int32_t rx0 = ox + (int32_t)((r->x - minX) * scale);
        int32_t ry0 = oy + (int32_t)((r->y - minY) * scale);
        int32_t rx1 = ox + (int32_t)((r->x - minX + r->w) * scale);
        int32_t ry1 = oy + (int32_t)((r->y - minY + r->h) * scale);
        bool visited = (snap->visitedMask >> (i & 63)) & 1;
        uint32_t fill = i == snap->room ? COL_ROOM_HERE : visited ? COL_ROOM_SEEN : COL_ROOM_UNSEEN;
        /* 1px inset keeps a dark seam between adjacent rooms so the layout
         * reads as separate rooms, not one blob. */
        FillRect(buf, rx0, ry0, rx1, ry1, COL_ROOM_EDGE);
        FillRect(buf, rx0 + 1, ry0 + 1, rx1 - 1, ry1 - 1, fill);
    }

    int32_t px = ox + (int32_t)((snap->playerX - minX) * scale);
    int32_t py = oy + (int32_t)((snap->playerY - minY) * scale);
    int32_t dot = (y1 - y0) / 40;
    if (dot < 3) dot = 3;
    uint32_t markerCol = (tick & 8) ? RGB(0xFF, 0xFF, 0xFF) : RGB(0x40, 0xE8, 0x40);
    FillRect(buf, px - dot / 2 - 1, py - dot / 2 - 1, px + dot / 2 + 1, py + dot / 2 + 1,
             RGB(0x00, 0x00, 0x00));
    FillRect(buf, px - dot / 2, py - dot / 2, px + dot / 2, py + dot / 2, markerCol);
}

/* Item grid: the pause menu's 16 equip slots as a 4x4 touch grid (three
 * rows of gear, bottles along the bottom — same slot order as the real
 * menu). Rebuilds sTapTargets to match wherever the cells actually landed
 * this frame. */
static void PaintItems(const ANativeWindow_Buffer* buf, const SecondScreenSnapshot* snap, int32_t x0,
                        int32_t y0, int32_t x1, int32_t y1) {
    const int cols = 4, rows = 4;
    int32_t cellW = (x1 - x0) / cols;
    int32_t cellH = (y1 - y0) / rows;
    int32_t cell = cellW < cellH ? cellW : cellH;
    int32_t gap = cell / 12;
    int32_t iconScale = (cell - 2 * gap - 8) / 16;
    if (iconScale < 1) iconScale = 1;
    int32_t tagScale = cell / 24;
    if (tagScale < 1) tagScale = 1;

    TapTarget targets[SECOND_SCREEN_ITEM_SLOTS];
    int targetCount = 0;

    for (int slot = 0; slot < SECOND_SCREEN_ITEM_SLOTS; slot++) {
        int32_t cx0 = x0 + (slot % cols) * cellW + gap;
        int32_t cy0 = y0 + (slot / cols) * cellH + gap;
        int32_t cx1 = cx0 + cell - 2 * gap;
        int32_t cy1 = cy0 + cell - 2 * gap;

        uint8_t itemId = snap->menuItems[slot];
        FillRect(buf, cx0, cy0, cx1, cy1, itemId ? COL_CELL_OWNED : COL_CELL_EMPTY);
        if (itemId == 0) {
            continue;
        }

        /* Bottles are containers; the icon players recognize is what's
         * inside (pause menu does the same substitution). */
        uint8_t iconId = itemId;
        if (slot >= 12) {
            uint8_t content = snap->bottleContents[slot - 12];
            if (content != 0) {
                iconId = content;
            }
        }
        int32_t iconX = (cx0 + cx1) / 2 - 8 * iconScale;
        int32_t iconY = (cy0 + cy1) / 2 - 8 * iconScale;
        Port_SecondScreenRender_DrawItemIcon((uint32_t*)buf->bits, buf->width, buf->height, buf->stride,
                                              iconX, iconY, iconScale, iconId);

        if (slot == snap->equippedSlotA || slot == snap->equippedSlotB) {
            OutlineRect(buf, cx0, cy0, cx1, cy1, gap > 2 ? 2 : 1, COL_EQUIP_BORDER);
            bool isA = slot == snap->equippedSlotA;
            int32_t tagW = 5 * tagScale, tagH = 7 * tagScale;
            int32_t tagX = isA ? cx0 : cx1 - tagW;
            FillRect(buf, tagX, cy0, tagX + tagW, cy0 + tagH, isA ? COL_TAG_A : COL_TAG_B);
            DrawGlyph(buf, tagX + tagScale, cy0 + tagScale, tagScale, isA ? 'A' : 'B', COL_TEXT);
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

static void PaintStatus(const ANativeWindow_Buffer* buf, const SecondScreenSnapshot* snap, int32_t x0,
                         int32_t y0, int32_t x1, int32_t y1) {
    int32_t hs = (y1 - y0) / 3;
    if (hs < 6) hs = 6;

    /* Hearts, 8 health units each, half-heart granularity like the HUD. */
    int32_t hx = x0;
    int fullHearts = snap->health / 8;
    bool halfHeart = (snap->health % 8) >= 4;
    int maxHearts = snap->maxHealth / 8;
    for (int i = 0; i < maxHearts && i < 10; i++) {
        FillRect(buf, hx, y0, hx + hs, y0 + hs, COL_HEART_EMPTY);
        if (i < fullHearts) {
            FillRect(buf, hx, y0, hx + hs, y0 + hs, COL_HEART_FULL);
        } else if (i == fullHearts && halfHeart) {
            FillRect(buf, hx, y0, hx + hs / 2, y0 + hs, COL_HEART_FULL);
        }
        hx += hs + hs / 3;
    }

    /* Rupee count, right-aligned with a green rupee diamond in front. */
    int32_t digitScale = hs / 5;
    if (digitScale < 1) digitScale = 1;
    int32_t ry = y1 - 5 * digitScale - digitScale;
    int32_t left = DrawNumber(buf, x1, ry, digitScale, snap->rupees, COL_TEXT);
    int32_t rh = 5 * digitScale;
    int32_t rcx = left - rh;
    for (int32_t i = 0; i <= rh / 2; i++) {
        FillRect(buf, rcx - i, ry + rh / 2 - i, rcx + i + 1, ry + rh / 2 - i + 1, COL_RUPEE);
        FillRect(buf, rcx - i, ry + rh / 2 + i, rcx + i + 1, ry + rh / 2 + i + 1, COL_RUPEE);
    }
}

/* Dual-screen layout in the zelda3-android style: live automap on the
 * left, touch inventory on the right, hearts/rupees along the bottom of
 * the item side. */
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

    FillRect(&buf, 0, 0, buf.width, buf.height, COL_BG);

    if (!snap.inGame) {
        pthread_mutex_lock(&sTapTargetMutex);
        sTapTargetCount = 0; /* no gameplay, no equip targets */
        pthread_mutex_unlock(&sTapTargetMutex);
        PaintIdle(&buf);
        ANativeWindow_unlockAndPost(window);
        return;
    }

    int32_t pad = buf.height / 32;
    int32_t split = buf.width * 45 / 100; /* map | items */
    int32_t statusH = buf.height / 7;

    PaintMap(&buf, &snap, pad, pad, split - pad, buf.height - pad, sTick);
    PaintItems(&buf, &snap, split + pad, pad, buf.width - pad, buf.height - statusH - pad);
    PaintStatus(&buf, &snap, split + pad, buf.height - statusH, buf.width - pad,
                buf.height - pad);

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
