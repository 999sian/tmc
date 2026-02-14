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
    if (addr >= 0x06000000u && addr < 0x06018000u) {
        gVram[addr - 0x06000000u] = v;
        return;
    }

    printf("gba_write8: unimplemented for address 0x%08X\n", addr);
}

u8 gba_read8(uint32_t addr) {
    if (addr >= 0x02000000u && addr < 0x02040000u)
        return gEwram[addr - 0x02000000u];
    if (addr >= 0x04000000u && addr < 0x04000400u)
        return gIoMem[addr - 0x04000000u];
    if (addr >= 0x06000000u && addr < 0x06018000u)
        return gVram[addr - 0x06000000u];
    if (gRomData && addr >= 0x08000000u && addr < 0x08000000u + gRomSize) {
        Port_LogRomAccess(addr, "gba_read8");
        return gRomData[addr - 0x08000000u];
    }

    printf("gba_read8: unimplemented for address 0x%08X\n", addr);
    return 0;
}

void gba_write16(uint32_t addr, uint16_t v) {
    if (addr >= 0x04000000u && addr < 0x040003FFu) {
        gIoMem[addr - 0x04000000u] = v & 0xFF;
        gIoMem[addr - 0x04000000u + 1] = (v >> 8) & 0xFF;
        return;
    }
    if (addr >= 0x02000000u && addr < 0x0203FFFFu) {
        gEwram[addr - 0x02000000u] = v & 0xFF;
        gEwram[addr - 0x02000000u + 1] = (v >> 8) & 0xFF;
        return;
    }
    // BG palette
    if (addr >= 0x05000000u && addr < 0x050001FFu) {
        gBgPltt[(addr - 0x05000000u) >> 1] = v;
        return;
    }
    // OBJ palette
    if (addr >= 0x05000200u && addr < 0x050003FFu) {
        gObjPltt[(addr - 0x05000200u) >> 1] = v;
        return;
    }
    // VRAM
    if (addr >= 0x06000000u && addr < 0x06017FFFu) {
        u32 off = addr - 0x06000000u;
        gVram[off] = v & 0xFF;
        gVram[off + 1] = (v >> 8) & 0xFF;
        return;
    }
    // OAM
    if (addr >= 0x07000000u && addr < 0x070003FFu) {
        gOamMem[(addr - 0x07000000u) >> 1] = v;
        return;
    }
    printf("gba_write16: unimplemented for address 0x%08X\n", addr);
}

u16 gba_read16(uint32_t addr) {
    if (addr >= 0x04000000u && addr < 0x040003FFu)
        return gIoMem[addr - 0x04000000u] | (gIoMem[addr - 0x04000000u + 1] << 8);
    if (addr >= 0x05000000u && addr < 0x050001FFu)
        return gBgPltt[(addr - 0x05000000u) >> 1];
    if (addr >= 0x05000200u && addr < 0x050003FFu)
        return gObjPltt[(addr - 0x05000200u) >> 1];
    if (addr >= 0x06000000u && addr < 0x06017FFFu) {
        u32 off = addr - 0x06000000u;
        return gVram[off] | (gVram[off + 1] << 8);
    }
    if (addr >= 0x07000000u && addr < 0x070003FFu)
        return gOamMem[(addr - 0x07000000u) >> 1];
    if (addr >= 0x02000000u && addr < 0x0203FFFFu)
        return gEwram[addr - 0x02000000u] | (gEwram[addr - 0x02000000u + 1] << 8);
    if (gRomData && addr >= 0x08000000u) {
        Port_LogRomAccess(addr, "gba_read16");
        u32 off = addr - 0x08000000u;
        if (off + 1 < gRomSize) {
            return gRomData[off] | (gRomData[off + 1] << 8);
        }
    }

    printf("gba_read16: unimplemented for address 0x%08X\n", addr);
    return 0;
}

void gba_write32(uint32_t addr, uint32_t v) {
    if (addr >= 0x04000000u && addr < 0x040003FDu) {
        gIoMem[addr - 0x04000000u] = v & 0xFF;
        gIoMem[addr - 0x04000000u + 1] = (v >> 8) & 0xFF;
        gIoMem[addr - 0x04000000u + 2] = (v >> 16) & 0xFF;
        gIoMem[addr - 0x04000000u + 3] = (v >> 24) & 0xFF;
        return;
    }
    // BG palette (0x05000000 - 0x050001FF)
    if (addr >= 0x05000000u && addr < 0x050001FEu) {
        gBgPltt[(addr - 0x05000000u) >> 1] = v & 0xFFFF;
        gBgPltt[((addr - 0x05000000u) >> 1) + 1] = (v >> 16) & 0xFFFF;
        return;
    }
    // BG/OBJ palette boundary straddle
    if (addr == 0x050001FEu) {
        gBgPltt[0xFF] = v & 0xFFFF;
        gObjPltt[0] = (v >> 16) & 0xFFFF;
        return;
    }
    // OBJ palette (0x05000200 - 0x050003FF)
    if (addr >= 0x05000200u && addr < 0x050003FEu) {
        gObjPltt[(addr - 0x05000200u) >> 1] = v & 0xFFFF;
        gObjPltt[((addr - 0x05000200u) >> 1) + 1] = (v >> 16) & 0xFFFF;
        return;
    }
    if (addr >= 0x06000000u && addr < 0x06017FFDu) {
        u32 off = addr - 0x06000000u;
        gVram[off] = v & 0xFF;
        gVram[off + 1] = (v >> 8) & 0xFF;
        gVram[off + 2] = (v >> 16) & 0xFF;
        gVram[off + 3] = (v >> 24) & 0xFF;
        return;
    }
    if (addr >= 0x07000000u && addr < 0x070003FDu) {
        gOamMem[(addr - 0x07000000u) >> 1] = v & 0xFFFF;
        gOamMem[(addr - 0x07000000u + 2) >> 1] = (v >> 16) & 0xFFFF;
        return;
    }
    if (addr >= 0x02000000u && addr < 0x0203FFFDu) {
        u32 off = addr - 0x02000000u;
        gEwram[off] = v & 0xFF;
        gEwram[off + 1] = (v >> 8) & 0xFF;
        gEwram[off + 2] = (v >> 16) & 0xFF;
        gEwram[off + 3] = (v >> 24) & 0xFF;
        return;
    }
    printf("gba_write32: unimplemented for address 0x%08X\n", addr);
}

u32 gba_read32(uint32_t addr) {
    if (addr >= 0x04000000u && addr < 0x040003FDu)
        return gIoMem[addr - 0x04000000u] | (gIoMem[addr - 0x04000000u + 1] << 8) |
               (gIoMem[addr - 0x04000000u + 2] << 16) | (gIoMem[addr - 0x04000000u + 3] << 24);
    if (addr >= 0x05000000u && addr < 0x050001FEu)
        return gBgPltt[(addr - 0x05000000u) >> 1] | (gBgPltt[((addr - 0x05000000u) >> 1) + 1] << 16);
    if (addr >= 0x05000200u && addr < 0x050003FEu)
        return gObjPltt[(addr - 0x05000200u) >> 1] | (gObjPltt[((addr - 0x05000200u) >> 1) + 1] << 16);
    if (addr >= 0x06000000u && addr < 0x06017FFDu) {
        u32 off = addr - 0x06000000u;
        return gVram[off] | (gVram[off + 1] << 8) | (gVram[off + 2] << 16) | (gVram[off + 3] << 24);
    }
    if (addr >= 0x07000000u && addr < 0x070003FDu)
        return gOamMem[(addr - 0x07000000u) >> 1] | (gOamMem[(addr - 0x07000000u + 2) >> 1] << 16);
    if (addr >= 0x02000000u && addr < 0x0203FFFDu) {
        u32 off = addr - 0x02000000u;
        return gEwram[off] | (gEwram[off + 1] << 8) | (gEwram[off + 2] << 16) | (gEwram[off + 3] << 24);
    }
    if (gRomData && addr >= 0x08000000u) {
        Port_LogRomAccess(addr, "gba_read32");
        u32 off = addr - 0x08000000u;
        if (off + 3 < gRomSize) {
            return gRomData[off] | (gRomData[off + 1] << 8) | (gRomData[off + 2] << 16) | (gRomData[off + 3] << 24);
        }
    }

    printf("gba_read32: unimplemented for address 0x%08X\n", addr);
    return 0;
}
