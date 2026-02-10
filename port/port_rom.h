#pragma once
#include "port_types.h"

// ROM data buffer
extern u8* gRomData;
extern u32 gRomSize;

// Load the ROM file and set up ROM-backed symbols
void Port_LoadRom(const char* path);

// ROM access logging - logs unique ROM addresses accessed at runtime
void Port_LogRomAccess(u32 gba_addr, const char* caller);
void Port_PrintRomAccessSummary(void);

/*
 * Read a packed 32-bit GBA ROM pointer from a base address at the given index.
 * On GBA, pointer tables store 4-byte pointers; on 64-bit PC, sizeof(void*)==8,
 * so we can't index them directly.  This reads 4 bytes at base + index*4,
 * resolves ROM data pointers to native, and returns NULL for GBA Thumb function
 * pointers (bit 0 set) which can't be called on PC.
 */
void* Port_ReadPackedRomPtr(const void* base, u32 index);

/**
 * Resolve a GBA ROM data address to a native PC pointer.
 * Returns &gRomData[gba_addr - 0x08000000] for valid ROM addresses, NULL otherwise.
 */
static inline void* Port_ResolveRomData(u32 gba_addr) {
    if (gba_addr >= 0x08000000u && gba_addr < 0x08000000u + gRomSize)
        return &gRomData[gba_addr - 0x08000000u];
    return NULL;
}

/*
 * Resolve a raw GBA EWRAM address (0x02xxxxxx) to a native PC pointer.
 *
 * On GBA, globals like gMapBottom/gMapTop live in EWRAM at fixed addresses.
 * On PC, they are standalone C globals NOT inside the gEwram[] buffer.
 * gba_TryMemPtr(0x02xxxxxx) returns &gEwram[offset], which is WRONG for them.
 *
 * This function checks for known EWRAM globals first, applying struct-layout
 * adjustments where needed (e.g. MapLayer's bgSettings pointer is 4 bytes on
 * GBA but 8 on 64-bit PC). Falls back to gba_TryMemPtr for unknown addresses.
 */
void* Port_ResolveEwramPtr(u32 gba_addr);
