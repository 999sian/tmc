#pragma once
#include "port_types.h"
#include "structures.h"

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
