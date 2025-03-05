/**
 * @file minishVillageTileSetManager.c
 * @ingroup Managers
 *
 * @brief Swap tileSet data in minish village depending on the position
 */
#include "manager/minishVillageTileSetManager.h"
#include "asm.h"
#include "common.h"
#include "main.h"
#include "game.h"
#include "assets/gfx_offsets.h"
#include "functions.h"

void MinishVillageTileSetManager_OnEnterRoom(void*);
bool32 MinishVillageTileSetManager_UpdateRoomGfxGroup(MinishVillageTileSetManager*);
void MinishVillageTileSetManager_LoadGfxGroup(u32);

// clang-format off
const u16 gMinishVillageTileSetManagerRegions[] = {
    0, 0x000, 0x020, 0x0E0, 0x0E0,
#ifdef EU
    1, 0x028, 0x1C8, 0x060, 0x080,
    2, 0x188, 0x278, 0x0E0, 0x0A0,
#else
    1, 0x000, 0x1D0, 0x080, 0x060,
    2, 0x170, 0x278, 0x0F8, 0x0A0,
#endif
    3, 0x310, 0x178, 0x0C0, 0x150,
    3, 0x340, 0x2C8, 0x060, 0x090,
    4, 0x1D0, 0x000, 0x200, 0x0E0,
    1, 0x108, 0x188, 0x0D0, 0x080,
    2, 0x1E8, 0x338, 0x050, 0x0C0,
    0xFF,
};
// clang-format on

typedef struct {
    u32 gfx;
    void* dest;
} MinishVillageTileSetManagerGfxInfo;

const MinishVillageTileSetManagerGfxInfo gMinishVillageTileSetManagerGfxInfos[][8] = {
    {
        { offset_gUnk_086AAEE0 + 0x1580, BG_SCREEN_ADDR(0) },
        { offset_gUnk_086AAEE0 + 0x2580, BG_SCREEN_ADDR(2) },
        { offset_gUnk_086AAEE0 + 0x3580, BG_SCREEN_ADDR(4) },
        { offset_gUnk_086AAEE0 + 0x4580, BG_SCREEN_ADDR(6) },
        { offset_gUnk_086AAEE0 + 0x5580, BG_SCREEN_ADDR(16) },
        { offset_gUnk_086AAEE0 + 0x6580, BG_SCREEN_ADDR(18) },
        { offset_gUnk_086AAEE0 + 0x7580, BG_SCREEN_ADDR(20) },
        { offset_gUnk_086AAEE0 + 0x8580, BG_SCREEN_ADDR(22) },
    },
    {
        { offset_gUnk_086AAEE0 + 0x9580, BG_SCREEN_ADDR(0) },
        { offset_gUnk_086AAEE0 + 0xA580, BG_SCREEN_ADDR(2) },
        { offset_gUnk_086AAEE0 + 0xB580, BG_SCREEN_ADDR(4) },
        { offset_gUnk_086AAEE0 + 0xC580, BG_SCREEN_ADDR(6) },
        { offset_gUnk_086AAEE0 + 0xD580, BG_SCREEN_ADDR(16) },
        { offset_gUnk_086AAEE0 + 0xE580, BG_SCREEN_ADDR(18) },
        { offset_gUnk_086AAEE0 + 0xF580, BG_SCREEN_ADDR(20) },
        { offset_gUnk_086AAEE0 + 0x10580, BG_SCREEN_ADDR(22) },
    },
    {
        { offset_gUnk_086AAEE0 + 0x11580, BG_SCREEN_ADDR(0) },
        { offset_gUnk_086AAEE0 + 0x12580, BG_SCREEN_ADDR(2) },
        { offset_gUnk_086AAEE0 + 0x13580, BG_SCREEN_ADDR(4) },
        { offset_gUnk_086AAEE0 + 0x14580, BG_SCREEN_ADDR(6) },
        { offset_gUnk_086AAEE0 + 0x15580, BG_SCREEN_ADDR(16) },
        { offset_gUnk_086AAEE0 + 0x16580, BG_SCREEN_ADDR(18) },
        { offset_gUnk_086AAEE0 + 0x17580, BG_SCREEN_ADDR(20) },
        { offset_gUnk_086AAEE0 + 0x18580, BG_SCREEN_ADDR(22) },
    },
    {
        { offset_gUnk_086AAEE0 + 0x19580, BG_SCREEN_ADDR(0) },
        { offset_gUnk_086AAEE0 + 0x1A580, BG_SCREEN_ADDR(2) },
        { offset_gUnk_086AAEE0 + 0x1B580, BG_SCREEN_ADDR(4) },
        { offset_gUnk_086AAEE0 + 0x1C580, BG_SCREEN_ADDR(6) },
        { offset_gUnk_086AAEE0 + 0x1D580, BG_SCREEN_ADDR(16) },
        { offset_gUnk_086AAEE0 + 0x1E580, BG_SCREEN_ADDR(18) },
        { offset_gUnk_086AAEE0 + 0x1F580, BG_SCREEN_ADDR(20) },
        { offset_gUnk_086AAEE0 + 0x20580, BG_SCREEN_ADDR(22) },
    },
    {
        { offset_gUnk_086AAEE0 + 0x21580, BG_SCREEN_ADDR(0) },
        { offset_gUnk_086AAEE0 + 0x22580, BG_SCREEN_ADDR(2) },
        { offset_gUnk_086AAEE0 + 0x23580, BG_SCREEN_ADDR(4) },
        { offset_gUnk_086AAEE0 + 0x24580, BG_SCREEN_ADDR(6) },
        { offset_gUnk_086AAEE0 + 0x25580, BG_SCREEN_ADDR(16) },
        { offset_gUnk_086AAEE0 + 0x26580, BG_SCREEN_ADDR(18) },
        { offset_gUnk_086AAEE0 + 0x27580, BG_SCREEN_ADDR(20) },
        { offset_gUnk_086AAEE0 + 0x28580, BG_SCREEN_ADDR(22) },
    },
};

const u8 gMinishVillateTileSetManagerPaletteGroups[] = { 0x16, 0x17, 0x17, 0x18, 0x18 };

void MinishVillageTileSetManager_Main(MinishVillageTileSetManager* this) {
    u32 gfxGroup;
    const MinishVillageTileSetManagerGfxInfo* gfxInfo;
    s32 super_timer;
    if (super->action == 0) {
        super->action = 1;
        super->timer = 8;
        this->unk_20 = 0xFF;

#ifndef EU
        SetEntityPriority((Entity*)this, PRIO_PLAYER_EVENT);
#endif
        RegisterTransitionHandler(this, MinishVillageTileSetManager_OnEnterRoom, NULL);
    }
#ifdef EU
    if (gRoomControls.reload_flags)
        return;
#endif

    if (MinishVillageTileSetManager_UpdateRoomGfxGroup(this)) {
        gfxGroup = (u32)gRoomVars.graphicsGroups[0];
        if (this->unk_20 != gfxGroup) {
            this->unk_20 = gfxGroup;
            super->timer = 0;
        }
    }
#ifndef EU
    if (gRoomControls.reload_flags)
        return;
#endif
#ifndef JP
#ifndef EU
    gfxGroup = this->unk_20;
#endif
#endif

    gfxInfo = gMinishVillageTileSetManagerGfxInfos[gfxGroup];
#ifdef EU
    super_timer = super->timer;
    if (super_timer == 0) {
#else
    switch (super->timer) {
        case 0:
#endif
        gPauseMenuOptions.disabled = 1;
        LoadResourceAsync(&gGlobalGfxAndPalettes[gfxInfo->gfx], gfxInfo->dest, BG_SCREEN_SIZE * 2);
        LoadPaletteGroup(gMinishVillateTileSetManagerPaletteGroups[gfxGroup]);
        super->timer++;
#ifdef EU
    } else {
        switch (super_timer) {
            case 0:
#else
            break;
#endif
            case 1:
            case 2:
            case 3:
            case 4:
            case 5:
            case 6:
            case 7:
                LoadResourceAsync(&gGlobalGfxAndPalettes[gfxInfo[super->timer].gfx], gfxInfo[super->timer].dest,
                                  BG_SCREEN_SIZE * 2);
                super->timer++;
#ifdef EU
                gPauseMenuOptions.disabled = 0;
#endif
                break;
            case 8:
#ifndef EU
                gPauseMenuOptions.disabled = 0;
                super->timer++;
#endif
                break;
        }
#ifdef EU
    }
#endif
}

void MinishVillageTileSetManager_OnEnterRoom(void* this) {
    MinishVillageTileSetManager_LoadGfxGroup(gRoomVars.graphicsGroups[0]);
}

bool32 MinishVillageTileSetManager_UpdateRoomGfxGroup(MinishVillageTileSetManager* this) {
    u32 gfxGroup = CheckRegionsOnScreen(gMinishVillageTileSetManagerRegions);
    if (gfxGroup != 0xFF) {
        gRoomVars.graphicsGroups[0] = gfxGroup;
        return TRUE;
    }
    return FALSE;
}

void MinishVillageTileSetManger_LoadInitialGfxGroup(void) {
    u32 gfxGroup = CheckRegionsOnScreen(gMinishVillageTileSetManagerRegions);
    if (gfxGroup != 0xFF) {
        MinishVillageTileSetManager_LoadGfxGroup(gfxGroup);
    }
}

void MinishVillageTileSetManager_LoadGfxGroup(u32 gfxGroup) {
    u32 i;
    const MinishVillageTileSetManagerGfxInfo* gfxInfo;

#ifndef EU
    if (gfxGroup >= ARRAY_COUNT(gMinishVillageTileSetManagerGfxInfos))
        return;
#endif

    LoadPaletteGroup(gMinishVillateTileSetManagerPaletteGroups[gfxGroup]);
    gfxInfo = gMinishVillageTileSetManagerGfxInfos[gfxGroup];
    for (i = 0; i < ARRAY_COUNT(gMinishVillageTileSetManagerGfxInfos[0]); i++, gfxInfo++) {
        DmaCopy32(3, &gGlobalGfxAndPalettes[gfxInfo->gfx], gfxInfo->dest, BG_SCREEN_SIZE * 2);
    }
    gRoomVars.graphicsGroups[0] = gfxGroup;
}
