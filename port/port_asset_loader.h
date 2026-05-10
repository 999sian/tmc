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
bool32 Port_IsRoomHeaderPtrReadable(const void* ptr);
bool32 Port_IsLoadedAssetBytes(const void* ptr, u32 size);
const u8* Port_GetMapAssetDataByIndex(u32 assetIndex, u32* size);
const u8* Port_GetSpriteAnimationData(u16 spriteIndex, u32 animIndex);

/* Try to mmap every assets dir's .pak file under the given path.
 * Returns the number of archives successfully mounted (0 if pak
 * loading is disabled, no .pak files exist, or any open failed). */
int Port_MountAssetPaks(const char* assetsRoot);

/* Unmount any previously-mounted paks and switch the loader back
 * into loose-file mode. Safe to call when nothing is mounted. */
void Port_UnmountAssetPaks(void);

/* True if at least one pak is currently mounted. */
bool32 Port_PaksMounted(void);

/* Total number of entries across all mounted paks. */
int Port_PakEntryCount(void);

/* Drop the cached asset-group state and re-scan from disk. tmc_pc
 * calls this after a fresh first-launch extraction so the engine
 * picks up assets that didn't exist when Port_LoadRom first probed
 * the disk. Idempotent. */
void Port_AssetLoader_Reload(void);

#ifdef TMC_OVERLAP_EXTRACT_INIT
/* Phase 7 hooks (compiled in only when the overlap-extract-init
 * build flag is set). The bootstrap calls BeginGated to shut all
 * phase gates before kicking off a cold-launch extraction, calls
 * OpenGate(category) as each pak category finishes, and calls
 * OpenAllGates once the entire extraction is done (or as a
 * failsafe on error). */
void Port_AssetLoader_BeginGated(void);
void Port_AssetLoader_OpenGate(int phaseGateIndex);
void Port_AssetLoader_OpenAllGates(void);
#endif

#ifdef __cplusplus
}
#endif
