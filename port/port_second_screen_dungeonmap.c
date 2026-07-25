#include "port_second_screen_dungeonmap.h"

#include <string.h>

/* Stub seam — the real ROM decode lands as its own change. Returning
 * "no data" keeps the compositor's schematic fallback active while
 * letting it integrate against the final API already. */

int Port_SecondScreenDungeonMap_GetInfo(uint8_t dungeonIdx, uint8_t area, uint8_t room,
                                        SecondScreenDungeonMapInfo* out) {
    (void)dungeonIdx;
    (void)area;
    (void)room;
    if (out) {
        memset(out, 0, sizeof(*out));
    }
    return 0;
}

int Port_SecondScreenDungeonMap_Draw(uint32_t* pixels, int32_t bufW, int32_t bufH, int32_t stride,
                                     int32_t dstX, int32_t dstY, int32_t dstW, int32_t dstH,
                                     uint8_t dungeonIdx, int32_t floor, uint8_t area, uint8_t room,
                                     uint64_t visitedMask, uint8_t dungeonItemBits,
                                     int32_t playerAreaX, int32_t playerAreaY, uint32_t tick) {
    (void)pixels;
    (void)bufW;
    (void)bufH;
    (void)stride;
    (void)dstX;
    (void)dstY;
    (void)dstW;
    (void)dstH;
    (void)dungeonIdx;
    (void)floor;
    (void)area;
    (void)room;
    (void)visitedMask;
    (void)dungeonItemBits;
    (void)playerAreaX;
    (void)playerAreaY;
    (void)tick;
    return 0;
}
