/**
 * @file worldEvent2.c
 * @ingroup WorldEvents
 *
 * @brief World Event 2
 */
#include "fade.h"
#include "flags.h"
#include "map.h"
#include "menu.h"
#include "room.h"
#include "sound.h"
#include "subtask.h"
#include "tiles.h"
#ifdef PC_PORT
#include "port/port_region_data.h"
#endif

void sub_08055994(void);

void WorldEvent_2_0(void);
void WorldEvent_2_1(void);
void WorldEvent_2_2(void);
void (*const WorldEvent_2_Functions[])(void) = {
    WorldEvent_2_0,
    WorldEvent_2_1,
    WorldEvent_2_2,
};

void WorldEvent_2(void) {
    WorldEvent_2_Functions[gMenu.overlayType]();
}

void WorldEvent_2_0(void) {
    u8* ptr = gMenu.field_0xc;
    sub_0804B0B0(gMenu.field_0xc[2], gMenu.field_0xc[3]);
    sub_0804B0E8(ptr[2], ptr[3]);
    sub_08055B70(gMenu.field_0x4, 0, 0, 0);
    gMenu.overlayType++;
    gUpdateVisibleTiles = 1;
    SetFade(FADE_INSTANT, 0x10);
}

void WorldEvent_2_1(void) {
    if ((gFadeControl.active == 0) && CheckRoomFlag(0xff)) {
        sub_08055994();
        gMenu.overlayType++;
        gMenu.transitionTimer = 120;
    }
}

void WorldEvent_2_2(void) {
    if (--gMenu.transitionTimer == 0) {
        gMenu.menuType++;
    }
}

void sub_08055994(void) {
    u32 layer;
#if defined(PC_PORT) && defined(MULTI_REGION)
    const TileEntity* list = (const TileEntity*)Port_ResolveRegionData(gUnk_080FEAC8);
    TileEntity* ptr = (TileEntity*)(list ? &list[gMenu.field_0x4] : &gUnk_080FEAC8[gMenu.field_0x4]);
#else
    TileEntity* ptr = &gUnk_080FEAC8[gMenu.field_0x4];
#endif
    if ((ptr->_6 & 1) != 0) {
        layer = LAYER_TOP;
    } else {
        layer = LAYER_BOTTOM;
    }
    SetTileType(TILE_TYPE_115,
                (*(u16*)(gMenu.field_0xc + 8) >> 4 & 0x3f) | (*(u16*)(gMenu.field_0xc + 10) >> 4 & 0x3f) << 6, layer);
    sub_080553E0((u32)gMenu.field_0x4);
    SoundReq(SFX_SECRET_BIG);
}
