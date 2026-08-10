#include <stdio.h>
#include <string.h>

#include "port_config.h"
#include "port_rom.h"

static int sFailures;

#define CHECK_EQ(actual, expected, message)                                                                      \
    do {                                                                                                         \
        unsigned got__ = (unsigned)(actual);                                                                     \
        unsigned want__ = (unsigned)(expected);                                                                  \
        if (got__ != want__) {                                                                                   \
            fprintf(stderr, "FAIL: %s: got 0x%X expected 0x%X\n", message, got__, want__);                      \
            sFailures++;                                                                                         \
        }                                                                                                        \
    } while (0)

#define CHECK_TRUE(condition, message)                                                                           \
    do {                                                                                                         \
        if (!(condition)) {                                                                                      \
            fprintf(stderr, "FAIL: %s\n", message);                                                            \
            sFailures++;                                                                                         \
        }                                                                                                        \
    } while (0)

static void WriteU32(u8* dest, u32 value) {
    dest[0] = (u8)value;
    dest[1] = (u8)(value >> 8);
    dest[2] = (u8)(value >> 16);
    dest[3] = (u8)(value >> 24);
}

int main(void) {
    static _Alignas(4) u8 rom[0x8400];
    const u16* shape;

    CHECK_EQ(Port_RemapFixedUiSpriteIndexForRegion(ROM_REGION_USA, 322u), 322u,
             "USA item UI sprite stays native");
    CHECK_EQ(Port_RemapFixedUiSpriteIndexForRegion(ROM_REGION_EU, 322u), 321u,
             "EU item UI sprite uses its shifted entry");
    CHECK_EQ(Port_RemapFixedUiSpriteIndexForRegion(ROM_REGION_USA, 505u), 505u,
             "USA HUD button sprite stays native");
    CHECK_EQ(Port_RemapFixedUiSpriteIndexForRegion(ROM_REGION_EU, 505u), 504u,
             "EU HUD button sprite uses its shifted entry");
    CHECK_EQ(Port_RemapFixedUiSpriteIndexForRegion(ROM_REGION_EU, 506u), 506u,
             "EU fixed UI remap is not a broad late-sprite shift");

    memset(rom, 0, sizeof(rom));
    WriteU32(rom + PORT_COLLISION_SHAPE_PTRS_USA, 0x08001000u);
    WriteU32(rom + PORT_COLLISION_SHAPE_PTRS_EU, 0x08001100u);
    memset(rom + 0x1000, 0xFF, 16u * sizeof(u16));
    memset(rom + 0x1100, 0, 16u * sizeof(u16));

    shape = Port_ResolveCollisionShapeFromRom(rom, sizeof(rom), PORT_COLLISION_SHAPE_PTRS_EU, 0);
    CHECK_TRUE(shape == (const u16*)(rom + 0x1100), "EU collision table resolves its EU-native target");
    CHECK_EQ(shape != NULL ? shape[0] : 1u, 0u, "EU passable collision mask stays clear");

    shape = Port_ResolveCollisionShapeFromRom(rom, sizeof(rom), PORT_COLLISION_SHAPE_PTRS_USA, 0);
    CHECK_TRUE(shape == (const u16*)(rom + 0x1000), "USA collision table resolves its USA-native target");
    CHECK_EQ(shape != NULL ? shape[0] : 0u, 0xFFFFu, "stale-region pointer would select the blocking mask");

    CHECK_TRUE(Port_ResolveCollisionShapeFromRom(NULL, sizeof(rom), PORT_COLLISION_SHAPE_PTRS_EU, 0) == NULL,
               "null ROM is rejected");
    CHECK_TRUE(Port_ResolveCollisionShapeFromRom(rom, sizeof(rom), PORT_COLLISION_SHAPE_PTRS_EU, 40) == NULL,
               "out-of-range collision mask index is rejected");
    CHECK_TRUE(Port_ResolveCollisionShapeFromRom(rom, PORT_COLLISION_SHAPE_PTRS_EU + 3u,
                                                 PORT_COLLISION_SHAPE_PTRS_EU, 0) == NULL,
               "truncated pointer table is rejected");
    WriteU32(rom + PORT_COLLISION_SHAPE_PTRS_EU + sizeof(u32), 0x08001101u);
    CHECK_TRUE(Port_ResolveCollisionShapeFromRom(rom, sizeof(rom), PORT_COLLISION_SHAPE_PTRS_EU, 1) == NULL,
               "unaligned collision mask target is rejected");

    if (sFailures != 0) {
        return 1;
    }
    printf("port_region_runtime_data_test: ALL PASS\n");
    return 0;
}
