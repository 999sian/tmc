/* Synthetic ROM fixtures exercise the production regional resolvers. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port_config.h"
#include "port_rom.h"
#include <assert.h>
u8* gRomData;
u32 gRomSize;
const RomOffsets* gRomOffsets;
#include "rom_table_resolvers.inc"

static void pointer(u32 offset, u32 target) {
    u32 value = 0x08000000u + target;
    memcpy(gRomData + offset, &value, 4);
}
int main(void) {
    const RomOffsets* regions[] = { &kRomOffsets_USA, &kRomOffsets_EU, &kRomOffsets_JP };
    const u32 shapes[] = { 0x823c, 0x82d4, 0x823c };
    const u32 properties[] = { 0x360, 0x3a8, 0x360 };
    const u32 fusers[] = { 0x1dcc, 0x1e74, 0x1dcc };
    const u32 rails[] = { 0xfed98, 0xfe2dc, 0xfea48 };
    gRomSize = 0x100000;
    gRomData = calloc(1, gRomSize);
    assert(gRomData);
    for (unsigned r = 0; r < 3; ++r) {
        memset(gRomData, 0, gRomSize);
        gRomOffsets = regions[r];
        pointer(shapes[r] + 17 * 4, 0x9000);
        gRomData[0x9000] = 0x40;
        assert(Port_GetCollisionShapeData(17) == (u16*)(gRomData + 0x9000));
        assert(Port_GetCollisionShapeData(40) == NULL);
        pointer(shapes[r] + 17 * 4, gRomSize - 2);
        assert(Port_GetCollisionShapeData(17) == NULL);
        pointer(shapes[r] + 17 * 4, 0x9001);
        assert(Port_GetCollisionShapeData(17) == NULL);
        gRomData[properties[r]] = 0x34;
        gRomData[properties[r] + 1] = 0x12;
        assert(Port_GetTileTypeProperty(0) == 0x1234);
        assert(Port_GetTileTypeProperty(0xffffffff) == 0);
        pointer(fusers[r] + 0x68 * 4, 0x9001); /* Odd byte-aligned records are legal. */
        assert(Port_GetFuserFusionData(0x68) == gRomData + 0x9001);
        assert(Port_GetFuserFusionData(120) == NULL);
        pointer(fusers[r] + 0x68 * 4, gRomSize - 5);
        assert(Port_GetFuserFusionData(0x68) == NULL);
        const u32 entityTables[] = { r == 1 ? 0x23d6 : 0x232e, r == 1 ? 0x23ea : 0x2342 };
        for (unsigned kind = 0; kind < 2; kind++) {
            u8* entry = gRomData + entityTables[kind] + 6;
            const u8 record[] = { 0x22, 0xff, 0xff, 0x68, 0x34, 0x12 };
            memcpy(entry, record, sizeof(record));
            assert(Port_GetEntityFuserData(kind ? 7 : 3, 0x22, 4, 5) == (((u64)0x1234 << 32) | 0x68));
            assert(Port_GetEntityFuserData(kind ? 7 : 3, 0x23, 4, 5) == 0);
            entry[3] = 120;
            assert(Port_GetEntityFuserData(kind ? 7 : 3, 0x22, 4, 5) == 0);
        }
        for (u32 i = 0; i < 3; i++) pointer(rails[r] + i * 4, 0x9000 + i * 4);
        for (u32 i = 0; i < 3; i++) assert(Port_GetLilypadRail(i) == gRomData + 0x9000 + i * 4);
        assert(Port_GetLilypadRail(3) == NULL);
        printf("%s regional ROM tables: PASS\n", regions[r]->gameCode);
    }
    gRomOffsets = NULL;
    assert(Port_GetCollisionShapeData(0) == NULL);
    assert(Port_GetTileTypeProperty(0) == 0);
    assert(Port_GetFuserFusionData(0) == NULL);
    assert(Port_GetLilypadRail(0) == NULL);
    free(gRomData);
    return 0;
}
