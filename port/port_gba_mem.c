#include "port_gba_mem.h"
#include <stdio.h>
#include <stdlib.h>

u8 gIoMem[0x400];
u8 gEwram[0x40000];
u16 gBgPltt[256];
u16 gObjPltt[256];
u16 gOamMem[0x400 / 2];
u8 gVram[0x18000];

void gba_write8(uint32_t addr, uint8_t v) {
    if (addr >= 0x02000000u && addr < 0x02040000u) {
        gEwram[addr - 0x02000000u] = v;
        return;
    }
    if (addr >= 0x04000000u && addr < 0x04000400u) {
        gIoMem[addr - 0x04000000u] = v;
        return;
    }

    printf("gba_write8: unimplemented for address 0x%08X\n", addr);
    abort();
}

u8 gba_read8(uint32_t addr) {
    printf("gba_read8: unimplemented for address 0x%08X\n", addr);
    abort();
    return 0;
}

void gba_write16(uint32_t addr, uint16_t v) {
    if (addr >= 0x04000000u && addr < 0x04000400u) {
        gIoMem[addr - 0x04000000u] = v & 0xFF;
        gIoMem[addr - 0x04000000u + 1] = (v >> 8) & 0xFF;
        return;
    }
    // BG palette
    if (addr >= 0x05000000u && addr < 0x05000200u) {
        gBgPltt[(addr - 0x05000000u) >> 1] = v;
        return;
    }
    // OBJ palette
    if (addr >= 0x05000200u && addr < 0x05000400u) {
        gObjPltt[(addr - 0x05000200u) >> 1] = v;
        return;
    }
    // OAM
    if (addr >= 0x07000000u && addr < 0x07000400u) {
        gOamMem[(addr - 0x07000000u) >> 1] = v;
        return;
    }
    printf("gba_write16: invalid address 0x%08X\n", addr);
    abort();
    return;
}

u16 gba_read16(uint32_t addr) {
    if (addr >= 0x04000000u && addr < 0x04000400u)
        return gIoMem[addr - 0x04000000u] | (gIoMem[addr - 0x04000000u + 1] << 8);
    if (addr >= 0x05000000u && addr < 0x05000200u)
        return gBgPltt[(addr - 0x05000000u) >> 1];

    if (addr >= 0x05000200u && addr < 0x05000400u)
        return gObjPltt[(addr - 0x05000200u) >> 1];

    if (addr >= 0x07000000u && addr < 0x07000400u)
        return gOamMem[(addr - 0x07000000u) >> 1];

    printf("gba_read16: invalid address 0x%08X\n", addr);
    abort();
    return 0;
}

void gba_write32(uint32_t addr, uint32_t v) {
    if (addr >= 0x04000000u && addr < 0x04000400u) {
        gIoMem[addr - 0x04000000u] = v & 0xFF;
        gIoMem[addr - 0x04000000u + 1] = (v >> 8) & 0xFF;
        gIoMem[addr - 0x04000000u + 2] = (v >> 16) & 0xFF;
        gIoMem[addr - 0x04000000u + 3] = (v >> 24) & 0xFF;
        return;
    }
    if (addr >= 0x05000000u && addr < 0x05000400u) {
        gBgPltt[(addr - 0x05000000u) >> 1] = v & 0xFFFF;
        gBgPltt[(addr - 0x05000000u + 2) >> 1] = (v >> 16) & 0xFFFF;
        return;
    }
    if (addr >= 0x07000000u && addr < 0x07000400u) {
        gOamMem[(addr - 0x07000000u) >> 1] = v & 0xFFFF;
        gOamMem[(addr - 0x07000000u + 2) >> 1] = (v >> 16) & 0xFFFF;
        return;
    }
    printf("gba_write32: invalid address 0x%08X\n", addr);
    abort();
}

u32 gba_read32(uint32_t addr) {
    if (addr >= 0x04000000u && addr < 0x04000400u)
        return gIoMem[addr - 0x04000000u] | (gIoMem[addr - 0x04000000u + 1] << 8) |
               (gIoMem[addr - 0x04000000u + 2] << 16) | (gIoMem[addr - 0x04000000u + 3] << 24);
    if (addr >= 0x05000000u && addr < 0x05000400u)
        return gBgPltt[(addr - 0x05000000u) >> 1] | (gBgPltt[(addr - 0x05000000u + 2) >> 1] << 16);
    if (addr >= 0x07000000u && addr < 0x07000400u)
        return gOamMem[(addr - 0x07000000u) >> 1] | (gOamMem[(addr - 0x07000000u + 2) >> 1] << 16);

    printf("gba_read32: invalid address 0x%08X\n", addr);
    abort();
    return 0;
}
