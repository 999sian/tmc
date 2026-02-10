#pragma once
#include "port_types.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _MSC_VER
#include <intrin.h>
#endif

extern u8 gIoMem[0x400];       // I/O Memory (0x04000000-0x040003FF)
extern u8 gEwram[0x40000];     // EWRAM (0x02000000-0x0203FFFF)
extern u16 gBgPltt[256];       // 0x200 bytes
extern u16 gObjPltt[256];      // 0x200 bytes
extern u16 gOamMem[0x400 / 2]; // 0x400 bytes (OAM)
extern u8 gVram[0x18000];      // 96 KB VRAM GBA (0x06000000-0x06017FFF)

// ROM data (loaded from baserom.gba)
extern u8* gRomData;
extern u32 gRomSize;

// ROM access logging (defined in port_rom.c)
void Port_LogRomAccess(u32 gba_addr, const char* caller);

void gba_write8(uint32_t addr, uint8_t v);
u8 gba_read8(uint32_t addr);
void gba_write16(uint32_t addr, uint16_t v);
u16 gba_read16(uint32_t addr);
void gba_write32(uint32_t addr, uint32_t v);
u32 gba_read32(uint32_t addr);

/*
 * gba_TryMemPtr — non-aborting address resolver.
 * Returns native pointer for known GBA ranges, NULL otherwise.
 */
static inline void* gba_TryMemPtr(uint32_t addr) {
    if (addr >= 0x02000000u && addr < 0x02040000u)
        return &gEwram[addr - 0x02000000u];
    if (addr >= 0x04000000u && addr < 0x04000400u)
        return &gIoMem[addr - 0x04000000u];
    if (addr >= 0x05000000u && addr < 0x05000200u)
        return &gBgPltt[(addr - 0x05000000u) >> 1];
    if (addr >= 0x05000200u && addr < 0x05000400u)
        return &gObjPltt[(addr - 0x05000200u) >> 1];
    if (addr >= 0x06000000u && addr < 0x06018000u)
        return &gVram[addr - 0x06000000u];
    if (addr >= 0x07000000u && addr < 0x07000400u)
        return &gOamMem[(addr - 0x07000000u) >> 1];
    if (gRomData && addr >= 0x08000000u && addr < 0x08000000u + gRomSize) {
        Port_LogRomAccess(addr, "gba_TryMemPtr");
        return &gRomData[addr - 0x08000000u];
    }
    return NULL;
}

static inline void* gba_MemPtr(uint32_t addr) {
    void* ptr = gba_TryMemPtr(addr);
    if (ptr)
        return ptr;
#if defined(__GNUC__)
    void* caller = __builtin_return_address(0);
    fprintf(stderr, "FATAL: gba_MemPtr: invalid address 0x%08X (called from %p)\n", addr, caller);
#elif defined(_MSC_VER)
    void* caller = _ReturnAddress();
    fprintf(stderr, "FATAL: gba_MemPtr: invalid address 0x%08X (called from %p)\n", addr, caller);
#else
    fprintf(stderr, "FATAL: gba_MemPtr: invalid address 0x%08X\n", addr);
#endif
    fflush(stderr);
#if defined(_MSC_VER)
    __debugbreak();
#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
    __asm__ volatile("int3");
#endif
    abort();
    return NULL;
}

/*
 * port_resolve_addr — resolve a value that may be a GBA address or native ptr.
 * Used by the DMA emulation layer.
 */
static inline void* port_resolve_addr(uintptr_t val) {
    /* Try GBA address ranges first (0x02000000–0x09FFFFFF) */
    if (val >= 0x02000000u && val < 0x0A000000u) {
        void* p = gba_TryMemPtr((uint32_t)val);
        if (p)
            return p;
    }
    /* Otherwise treat as native pointer */
    return (void*)val;
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

static inline void port_MemCopyToGBA(const void* src, u32 destAddr, u32 size) {
    void* dest = gba_TryMemPtr(destAddr);
    if (src != NULL && dest != NULL) {
        for (u32 i = 0; i < size; i++) {
            ((u8*)dest)[i] = ((const u8*)src)[i];
        }
    }
}