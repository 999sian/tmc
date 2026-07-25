#include "port_second_screen_worldmap.h"

#include <stddef.h>

/* Stub seam — the real ROM decode lands as its own change. Returning
 * "not ready" keeps the compositor's schematic fallback active while
 * letting it integrate against the final API already. */

const uint32_t* Port_SecondScreenWorldMap_GetImage(int32_t* outW, int32_t* outH) {
    if (outW) {
        *outW = 0;
    }
    if (outH) {
        *outH = 0;
    }
    return NULL;
}

int Port_SecondScreenWorldMap_LocatePlayer(uint8_t area, int32_t areaX, int32_t areaY,
                                           int32_t* outMapX, int32_t* outMapY) {
    (void)area;
    (void)areaX;
    (void)areaY;
    (void)outMapX;
    (void)outMapY;
    return 0;
}

int Port_SecondScreenWorldMap_GetWindcrestPin(int32_t windcrestId, int32_t* outMapX, int32_t* outMapY) {
    (void)windcrestId;
    (void)outMapX;
    (void)outMapY;
    return 0;
}
