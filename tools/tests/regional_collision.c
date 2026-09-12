#include <stdio.h>
#include <stdlib.h>
#include "port_config.h"
#include "stubs_autogen.c"
static unsigned expected;
static u32 capture(Entity* a, Entity* b, u32 d, PortColSettings* settings) {
    (void)a;
    (void)b;
    (void)d;
    return (u8*)settings == gRomData + expected;
}
PortCollisionHandler gCollisionHandlers[23] = { capture, capture, capture, capture, capture, capture, capture, capture,
                                                capture, capture, capture, capture, capture, capture, capture, capture,
                                                capture, capture, capture, capture, capture, capture, capture };
u32 CalculateDirectionTo(s32 a, s32 b, s32 c, s32 d) {
    return 0;
}
int main(int argc, char** argv) {
    const RomOffsets* regions[] = { &kRomOffsets_USA, &kRomOffsets_EU, &kRomOffsets_JP };
    const unsigned starts[] = { 0xb7b74, 0xb729c, 0xb7914 };
    gRomSize = 0x1000000;
    gRomData = calloc(1, gRomSize);
    int failed = 0;
    for (unsigned r = 0; r < 3; r++) {
        gRomOffsets = regions[r];
        Entity a = { 0 }, b = { 0 };
        a.hurtType = 1;
        b.hitType = 0x68;
        expected = starts[r] + 12 * (34 * b.hitType + a.hurtType);
        unsigned result = PortCalcCollision(&a, &b);
        printf("%s collision settings beyond short asset: %s\n", regions[r]->gameCode, result ? "PASS" : "FAIL");
        failed |= !result;
    }
    free(gRomData);
    return failed;
}
