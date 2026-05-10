#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "global.h"

bool32 Port_LoadPaletteGroupFromAssets(u32 group);
bool32 Port_LoadGfxGroupFromAssets(u32 group);
bool32 Port_LoadAreaTablesFromAssets(void);
bool32 Port_LoadSpritePtrsFromAssets(void);
bool32 Port_LoadTextsFromAssets(void);
bool32 Port_AreSpritePtrsLoadedFromAssets(void);
void Port_LogAssetLoaderStatus(void);
void Port_LogTextLookup(u32 langIndex, u32 textIndex);
bool32 Port_RefreshAreaDataFromAssets(u32 area);
bool32 Port_IsAreaTablePtrFromAssets(u32 area, const void* ptr);
bool32 Port_IsRoomHeaderPtrReadable(const void* ptr);
bool32 Port_IsLoadedAssetBytes(const void* ptr, u32 size);
const u8* Port_GetMapAssetDataByIndex(u32 assetIndex, u32* size);
const u8* Port_GetSpriteAnimationData(u16 spriteIndex, u32 animIndex);

#ifdef __cplusplus
}
#endif
