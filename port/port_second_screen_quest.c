#include "port_second_screen_quest.h"

/* Stub seam — the real decode lands as its own change. Returning "not
 * ready" keeps the compositor's fallback active meanwhile. */

int Port_SecondScreenQuest_Draw(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                int32_t dstX, int32_t dstY, int32_t dstW, int32_t dstH,
                                const SecondScreenSnapshot* snap, uint32_t tick) {
    (void)pixels;
    (void)bufW;
    (void)bufH;
    (void)stride;
    (void)dstX;
    (void)dstY;
    (void)dstW;
    (void)dstH;
    (void)snap;
    (void)tick;
    return 0;
}
