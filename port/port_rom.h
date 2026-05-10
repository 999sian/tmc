#pragma once
#include <string.h>
#include "port_types.h"
#include "structures.h"

// ROM data buffer
extern u8* gRomData;
extern u32 gRomSize;

// Load the ROM file and set up ROM-backed symbols
void Port_LoadRom(const char* path);

/*
 * Probe the same candidate locations Port_LoadRom would and return a
 * pointer to a static buffer holding the absolute path of the first
 * reachable ROM file, or NULL if none of the known candidate names
 * are openable. Probe order matches Port_LoadRom's load order so a
 * successful probe guarantees a successful load. Intended for the
 * pre-window check in port_main.c so we can show an SDL message box
 * (and exit cleanly) before any window is created.
 */
const char* Port_FindBaseRomPath(void);

// Re-resolve a single area's room/tile/property tables from immutable ROM offsets.
void Port_RefreshAreaData(u32 area);

bool32 Port_IsAreaTablePtrReadable(u32 area, const void* ptr);

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

/**
 * Read entry [idx] from a packed-GBA-pointer table stored as a raw u8 array.
 *
 * Many `gUnk_08xxxxxx` tables in port/data_const_stubs.c are byte arrays of
 * 4-byte GBA pointers. Game code declares them externally as `T*[]` so
 * `arr[idx]` reads sizeof(T*) bytes — fine on the 32-bit GBA, broken on
 * x86-64 (reads 8 bytes, gets two GBA addresses concatenated → garbage).
 *
 * Use this helper at PC call sites to manually unpack the 4-byte entry and
 * resolve to a native pointer via the ROM mmap. (#16, #19 root cause.)
 */
static inline void* Port_PackedRomEntry(const void* base, u32 idx) {
    u32 raw;
    memcpy(&raw, (const u8*)base + idx * 4, 4);
    return Port_ResolveRomData(raw);
}

static inline u16 Port_ReadU16(const void* data) {
    const u8* raw = (const u8*)data;
    return (u16)(raw[0] | (raw[1] << 8));
}

static inline u32 Port_ReadU32(const void* data) {
    const u8* raw = (const u8*)data;
    return (u32)raw[0] | ((u32)raw[1] << 8) | ((u32)raw[2] << 16) | ((u32)raw[3] << 24);
}

/*
 * Read entry [index] from a packed-GBA-pointer ROM table and resolve to
 * a native pointer. Equivalent to Port_PackedRomEntry but kept for the
 * call sites that game code uses (matheo/master); the two helpers exist
 * because they were introduced in parallel branches before being merged.
 */
static inline void* Port_UnpackRomDataPtr(const void* table, u32 index) {
    return Port_ResolveRomData(Port_ReadU32((const u8*)table + index * 4));
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

/*
 * Decode a GBA-format Font struct (24 bytes, 32-bit pointers) into
 * a native Font struct (with 64-bit pointers, properly resolved).
 *
 * GBA Font layout (24 bytes):
 *   [0..3]  u32 dest        (EWRAM/BG buffer pointer)
 *   [4..7]  u32 gfx_dest    (VRAM pointer)
 *   [8..11] u32 buffer_loc  (EWRAM pointer)
 *   [12..15] u32 _c
 *   [16..17] u16 gfx_src
 *   [18] u8 width
 *   [19] u8 bitfield (right_align:1, sm_border:1, unused:1, draw_border:1, border_type:4)
 *   [20] u8 fill_type
 *   [21] u8 charColor
 *   [22] u8 _16
 *   [23] u8 stylized
 */
void Port_DecodeFontGBA(const void* gba_data, Font* out);

/*
 * Detect if a Font pointer actually points to a raw 24-byte GBA blob
 * rather than a native 64-bit Font struct.
 *
 * Heuristic: bytes [4..7] would be the high 32 bits of a native pointer,
 * which on x86_64 is always in the 0x00000000-0x00007FFF range.
 * For GBA data, bytes [4..7] are gfx_dest (VRAM: 0x06xxxxxx),
 * which falls in the 0x02000000-0x07FFFFFF range.
 */
static inline bool Port_IsFontGBAEncoded(const void* data) {
    const u8* raw = (const u8*)data;
    u32 word1 = raw[4] | (raw[5] << 8) | (raw[6] << 16) | (raw[7] << 24);
    return (word1 >= 0x02000000u && word1 < 0x08000000u);
}

/*
 * Return a stable, ROM-resolved SpritePtr entry for the given sprite index.
 * Returns NULL if the index is outside the loaded sprite table.
 */
const SpritePtr* Port_GetSpritePtr(u16 sprite_idx);
