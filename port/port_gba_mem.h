#pragma once
#include "port_types.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

extern u8 gIoMem[0x400];       // I/O Memory (0x04000000-0x040003FF)
extern u8 gEwram[0x40000];     // EWRAM (0x02000000-0x0203FFFF)
extern u16 gBgPltt[256];       // 0x200 bytes
extern u16 gObjPltt[256];      // 0x200 bytes
extern u16 gOamMem[0x400 / 2]; // 0x400 bytes (OAM)
extern u8 gVram[0x18000];      // 96 KB VRAM GBA (0x06000000-0x06017FFF)

void gba_write8(uint32_t addr, uint8_t v);
u8 gba_read8(uint32_t addr);
void gba_write16(uint32_t addr, uint16_t v);
u16 gba_read16(uint32_t addr);
void gba_write32(uint32_t addr, uint32_t v);
u32 gba_read32(uint32_t addr);

static inline void* gba_MemPtr(uint32_t addr) {
    if (addr >= 0x02000000u && addr < 0x02040000u) {
        return &gEwram[addr - 0x02000000u];
    }
    if (addr >= 0x04000000u && addr < 0x04000400u) {
        return &gIoMem[addr - 0x04000000u];
    }
    if (addr >= 0x05000000u && addr < 0x05000200u) {
        return &gBgPltt[(addr - 0x05000000u) >> 1];
    }
    // OBJ palette
    if (addr >= 0x05000200u && addr < 0x05000400u) {
        return &gObjPltt[(addr - 0x05000200u) >> 1];
    }
    // VRAM
    if (addr >= 0x06000000u && addr < 0x06018000u) {
        return &gVram[addr - 0x06000000u];
    }
    // OAM
    if (addr >= 0x07000000u && addr < 0x07000400u) {
        return &gOamMem[(addr - 0x07000000u) >> 1];
    }

    printf("gba_MemPtr: unimplemented for address 0x%08X\n", addr);
    return NULL;
}

static inline void gba_MemClear(u32 addr, u32 size) {
    void* ptr = gba_MemPtr(addr);
    if (ptr != NULL) {
        for (u32 i = 0; i < size; i++) {
            ((u8*)ptr)[i] = 0;
        }
    }
}

static inline void gba_MemCopy(u32 srcAddr, u32 destAddr, u32 size) {
    void* src = gba_MemPtr(srcAddr);
    void* dest = gba_MemPtr(destAddr);
    if (src != NULL && dest != NULL) {
        for (u32 i = 0; i < size; i++) {
            ((u8*)dest)[i] = ((u8*)src)[i];
        }
    }
}