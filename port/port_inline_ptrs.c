/*
 * port_inline_ptrs.c — patch ROM-region inline pointers that the asset
 * extractor doesn't include in any .bin file.
 *
 * Several `gUnk_additional_*` room-property tables in entity_headers.s
 * interleave `.incbin` chunks with `.4byte gUnk_080X_XXXX` rail-data
 * pointers. The asset extractor records each `.incbin` as a separate
 * file and the rail-pointer bytes between them fall into the gaps.
 *
 * On a developer machine those gaps are filled by `Port_LoadRom` with
 * the bytes from baserom.gba (or by extracted rom_data/ pages). On
 * users' release tarballs neither of those is present at runtime, so
 * the gap bytes stay zero, room-property iterators see NULL rail
 * pointers, and rail-bound entities (lava platforms, moving platforms,
 * etc.) don't move. (#36 root cause for Cave-of-Flames Rollobite +
 * BossDoor; the same pattern affects other rooms.)
 *
 * Patch the gap bytes unconditionally at ROM-init time so the runtime
 * sees the GBA-original layout regardless of which load path populated
 * gRomData. Each (offset, value) pair below mirrors a `.4byte` directive
 * in entity_headers.s; values that are also written by baserom.gba
 * round-trip identically.
 */

#include <stddef.h>
#include <stdint.h>

extern uint8_t* gRomData;
extern uint32_t gRomSize;

typedef struct {
    uint32_t offset;
    uint32_t value;
} InlinePtrEntry;

/* Sourced from entity_headers.s. Each entry's .value is the GBA address
 * of the rail-data symbol referenced by the `.4byte` directive. */
static const InlinePtrEntry kInlinePtrs[] = {
    /* AREA_CAVE_OF_FLAMES, Room 0x08 (Rollobite). The table starts with a
     * leading rail pointer right before the .incbin. (#36 — moving lava
     * platforms in the rollobite room render but never move.) */
    { 0x000E09DC, 0x080E09FC },

    /* AREA_CAVE_OF_FLAMES, Room 0x18 (BossDoor). Eight interleaved rail
     * pointers between the nine BossDoor.bin chunks. (#36 — moving lava
     * platforms in the boss-door corridor are missing entirely.) */
    { 0x000E15E4, 0x080E1674 },
    { 0x000E15F4, 0x080E16AA },
    { 0x000E1604, 0x080E16CE },
    { 0x000E1614, 0x080E16EC },
    { 0x000E1624, 0x080E170A },
    { 0x000E1634, 0x080E16EC },
    { 0x000E1644, 0x080E170A },
    { 0x000E1654, 0x080E16EC },
};

#define NUM_INLINE_PTRS (sizeof(kInlinePtrs) / sizeof(kInlinePtrs[0]))

/* Called from Port_LoadRom after assets + baserom.gba (if any) are in
 * gRomData. Idempotent — writes the canonical value over whatever's
 * there. */
void Port_PatchInlinePtrs(void) {
    if (gRomData == 0) {
        return;
    }
    for (size_t i = 0; i < NUM_INLINE_PTRS; ++i) {
        uint32_t off = kInlinePtrs[i].offset;
        if (off + 4 > gRomSize) {
            continue;
        }
        uint32_t v = kInlinePtrs[i].value;
        gRomData[off + 0] = (uint8_t)(v & 0xFF);
        gRomData[off + 1] = (uint8_t)((v >> 8) & 0xFF);
        gRomData[off + 2] = (uint8_t)((v >> 16) & 0xFF);
        gRomData[off + 3] = (uint8_t)((v >> 24) & 0xFF);
    }
}
