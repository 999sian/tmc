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

/* Cheap deterministic id -> color hash, just enough to tell "different
 * room/item" apart visually without a real palette (Phase 3d replaces this
 * with the game's own extracted tile/icon graphics). */
static uint32_t ColorForId(uint32_t id) {
    uint32_t h = id * 2654435761u; /* Knuth multiplicative hash */
    uint8_t r = 64 + (uint8_t)(h & 0xFF) / 2;
    uint8_t g = 64 + (uint8_t)((h >> 8) & 0xFF) / 2;
    uint8_t b = 64 + (uint8_t)((h >> 16) & 0xFF) / 2;
    return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

/* Phase 3c: proves the cross-thread game-state snapshot (port_second_screen_state.c)
 * with the simplest possible visualization — bars/blocks, not real digits,
 * to avoid pulling in a font-rendering dependency for a plumbing check.
 * Phase 3d replaces this with the actual minimap/item-icon compositor. */
static void PaintFrame(ANativeWindow* window) {
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

    FillRect(&buf, 0, 0, buf.width, buf.height, 0xFF101010u); /* dark background */

    /* Row 1: player X, as a bar width (wraps every 400px of world space —
     * rooms are usually smaller than that, so this visibly resets/grows as
     * you walk, rather than pinning to full width almost immediately). */
    int32_t barW = (snap.playerX % 400) * buf.width / 400;
    if (barW < 0) barW += buf.width; /* playerX can be negative near room origin */
    FillRect(&buf, 0, 20, barW, 60, 0xFF4040E0u);

    /* Row 2: player Y, same scale. */
    int32_t barH = (snap.playerY % 400) * buf.width / 400;
    if (barH < 0) barH += buf.width;
    FillRect(&buf, 0, 80, barH, 120, 0xFF40E040u);

    /* Room/area block: color hash of (area, room) — changes distinctly on
     * every room transition. */
    FillRect(&buf, 20, 160, 220, 260, ColorForId(((uint32_t)snap.area << 8) | snap.room));

    /* Equipped A/B item icons — real sprite-322 graphics, not color blocks
     * (see port_second_screen_render.c for why this reads ROM/asset data
     * directly instead of the live gPaletteBuffer). Native icons are only
     * 16x16px — scaled 4x (nearest-neighbor) to be legible on a real panel.
     * A dim placeholder square marks an empty slot so "no item equipped" is
     * still visible. */
    const int32_t iconScale = 4;
    const int32_t iconSize = 16 * iconScale;
    if (snap.equippedA == 0) {
        FillRect(&buf, 260, 160, 260 + iconSize, 160 + iconSize, 0xFF303030u);
    } else {
        Port_SecondScreenRender_DrawItemIcon((uint32_t*)buf.bits, buf.width, buf.height, buf.stride, 260, 160,
                                              iconScale, snap.equippedA);
    }
    if (snap.equippedB == 0) {
        FillRect(&buf, 260 + iconSize + 20, 160, 260 + 2 * iconSize + 20, 160 + iconSize, 0xFF303030u);
    } else {
        Port_SecondScreenRender_DrawItemIcon((uint32_t*)buf.bits, buf.width, buf.height, buf.stride,
                                              260 + iconSize + 20, 160, iconScale, snap.equippedB);
    }

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

#else /* !__ANDROID__ — no second display; every entry point is a no-op. */

void Port_SecondScreen_Init(void) {}
void Port_SecondScreen_OnSurfaceReady(void* window, int width, int height) {
    (void)window;
    (void)width;
    (void)height;
}
void Port_SecondScreen_OnSurfaceLost(void) {}

#endif
