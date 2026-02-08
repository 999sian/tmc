#include "common.h"
#include "fileselect.h"
#include "main.h"
#include "structures.h"
#include <stdio.h>

extern u32 gFixedTypeGfxData[];

void ReserveGFXSlots(u32, u32, u32);
void sub_080ADE74(u32);
void sub_080ADE24(void);
u32 FindFreeGFXSlots(u32);
void CleanUpGFXSlots(void);
void sub_080ADDD8(u32, u32);
void sub_080AE0C8(u32, Entity*, u32);
void SetGFXSlotStatus(GfxSlot*, u32);
u32 FindNextOccupiedGFXSlot(u32);
u32 FindFirstFreeGFXSlot(void);
void sub_080AE218(u32, u32);
void MoveGFXSlots(u32, u32);

void ResetPalettes(void) {
    GfxSlot* slots;
    GfxSlot* slot;
    u32 index;
    MemClear(&gGFXSlots, sizeof(gGFXSlots));
    // Reserve the first four slots for palettes.
    for (index = 0; index < 4; index++) {
        slots = gGFXSlots.slots;
        slot = &slots[index];
        ReserveGFXSlots(index, 0, 1);
        slot->status = GFX_SLOT_PALETTE;
        slot->referenceCount = 0x80;
    }
}

void sub_080ADD70(void) {
    u32 index;
    GfxSlot* slot;
    if (gGFXSlots.unk0 != 0) {
#ifndef EU
        if (gGFXSlots.unk_3 != 0) {
            sub_080ADE24();
        } else {
#endif
            index = 0;
            for (index = 0; index < MAX_GFX_SLOTS; index++) {
                slot = &gGFXSlots.slots[index];
                switch (slot->status) {
                    case GFX_SLOT_STATUS2:
                        slot->status = GFX_SLOT_UNLOADED;
                        break;
                    case GFX_SLOT_RESERVED:
                    case GFX_SLOT_GFX:
                    case GFX_SLOT_PALETTE:
                        if (slot->vramStatus == GFX_VRAM_3) {
                            // DEBUG:
                            {
                                u32 destAddr = index * 0x200 + 0x06010000 + 0x2800;
                                u32 sz = (u32)slot->paletteIndex << 5;
                                if (destAddr <= 0x06014140 && destAddr + sz > 0x06014140) {
                                    fprintf(stderr,
                                            "[GFX-UP] tick=%u sub_080ADD70 uploading slot %u "
                                            "dest=0x%X size=%u (covers tile522)\n",
                                            (u32)gMain.ticks, index, destAddr, sz);
                                }
                            }
                            sub_080ADE74(index);
                        }
                        break;
                }
            }
#ifndef EU
        }
#endif
    }
}

void sub_080ADDD8(u32 index, u32 paletteIndex) {
    GfxSlot* slot = &gGFXSlots.slots[index];
    u32 temp;
    slot->palettePointer = gGlobalGfxAndPalettes + (paletteIndex & 0xfffffc);
    if ((paletteIndex & 1) != 0) {
        temp = 0xffff;
    } else {
        // TODO probably a bitfield
        temp = ((paletteIndex) & 0x7f000000) >> 0x14;
    }
    slot->paletteIndex = temp;
    slot->vramStatus = GFX_VRAM_3;
}

void sub_080ADE24(void) {
    u32 index;
    GfxSlot* slot;
    gGFXSlots.unk_3 = 1;
    for (index = 0; index < MAX_GFX_SLOTS; index++) {
        slot = &gGFXSlots.slots[index];
        switch (slot->status) {
            case GFX_SLOT_FOLLOWER:
                break;
            case GFX_SLOT_RESERVED:
            case GFX_SLOT_GFX:
            case GFX_SLOT_PALETTE:
                sub_080ADE74(index);
                break;
            default:
                MemClear(slot, sizeof(GfxSlot));
                break;
        }
    }
    gGFXSlots.unk_3 = 0;
}

// Transfer gfx slot data to vram?
void sub_080ADE74(u32 index) {
    void* dest;
    GfxSlot* slot;
    struct_gUnk_020000C0_1* ptr1;
    s32 palIndex;
    s32 loopIndex;
    s32 tmp1;

    slot = gGFXSlots.slots + index;
    if (slot->vramStatus != 0) {
        slot->vramStatus = 1;
        if (((slot->paletteIndex != 0xffff) && (slot->unk_3 != 0))) {
            ptr1 = (struct_gUnk_020000C0_1*)(gUnk_020000C0 + slot->unk_3);
            for (loopIndex = 4; loopIndex > 0; loopIndex--) {
                if (ptr1->unk_00.unk2 != 0 && (gGFXSlots.unk_3 != 0 || ptr1->unk_00.unk3 != 0)) {
                    ptr1->unk_00.unk3 = 0;
                    palIndex = ptr1->unk_08.BYTES.byte1 << 5;
                    if (palIndex != 0) {
                        dest = (void*)(*(u16*)((s32)&ptr1->unk_08 + 2) * 0x20 + OBJ_VRAM0);
                        fprintf(stderr, "[VRAM] slot %u path-A dest=0x%08X size=%d src=%p\n", index,
                                (u32)(uintptr_t)dest, palIndex, (void*)ptr1->unk_0C);
                        DmaCopy32(3, ptr1->unk_0C, dest, palIndex);
                    }
                }
                ptr1++;
            }
        } else {
            dest = (void*)(index * 0x200 + OBJ_VRAM0 + 0x2800);
            switch (slot->paletteIndex) {
                default:
                    fprintf(stderr, "[VRAM] slot %u DMA: src=%p dest=0x%08X size=%u (tiles=%u)\n", index,
                            (void*)slot->palettePointer, (u32)(uintptr_t)dest, (u32)slot->paletteIndex << 5,
                            (u32)slot->paletteIndex);
                    // Debug:
                    {
                        const u8* s = (const u8*)slot->palettePointer;
                        u32 sz = (u32)slot->paletteIndex << 5;
                        int srcNonZero = 0;
                        for (u32 b = 0; b < sz && b < 8192; b++) {
                            if (s[b] != 0)
                                srcNonZero++;
                        }
                        fprintf(stderr, "[VRAM]   src data: %d/%u non-zero bytes\n", srcNonZero, sz);
                        if (sz > 4416) {
                            /* Check data at offset 4416 (tile 522 rel to slot 4) */
                            int nz2 = 0;
                            for (int b = 4416; b < 4416 + 32 && b < (int)sz; b++) {
                                if (s[b] != 0)
                                    nz2++;
                            }
                            fprintf(stderr, "[VRAM]   src[4416..4448] (tile522-area): %d/32 non-zero\n", nz2);
                        }
                    }
                    DmaCopy32(3, slot->palettePointer, dest, (u32)slot->paletteIndex << 5);
                    /* Verify the DMA actually wrote to VRAM */
                    {
                        u32 destGBA = (u32)(uintptr_t)dest;
                        if (destGBA >= 0x06010000u && destGBA < 0x06018000u) {
                            u32 vramOff = destGBA - 0x06000000u;
                            u32 sz2 = (u32)slot->paletteIndex << 5;
                            int postNZ = 0;
                            for (u32 b = 0; b < sz2 && vramOff + b < 0x18000; b++) {
                                if (gVram[vramOff + b] != 0)
                                    postNZ++;
                            }
                            fprintf(stderr, "[VRAM]   post-DMA verify: gVram[0x%05X], %d/%u non-zero\n", vramOff,
                                    postNZ, sz2);
                            /* Check tile 522 area specifically */
                            if (vramOff <= 0x14140 && vramOff + sz2 > 0x14140) {
                                int nz3 = 0;
                                for (int b = 0; b < 32; b++) {
                                    if (gVram[0x14140 + b] != 0)
                                        nz3++;
                                }
                                fprintf(stderr, "[VRAM]   tile522@gVram[0x14140]: %d/32 non-zero\n", nz3);
                            }
                        }
                    }
                    palIndex = slot->paletteIndex;
                    palIndex -= 0x10;
                    break;
                case 0:
                    slot->vramStatus = 0;
                    return;
                case 0xffff:
                    if (slot->unk_3 == 0) {
                        fprintf(stderr, "[VRAM] slot %u LZ77 to dest=0x%08X\n", index, (u32)(uintptr_t)dest);
                        LZ77UnCompVram(slot->palettePointer, dest);
                    }
                    return;
            }
            while (palIndex > 0) {
                slot++;
                slot[0].paletteIndex = 0;
                palIndex -= 0x10;
            }
        }
    }
}

bool32 LoadFixedGFX(Entity* entity, u32 gfxIndex) {
#ifdef EU
    GfxSlot* slot;
    u32 index;
    u32 count;
    u32 result;
    u32 data;
    if (gfxIndex == 0) {
        result = TRUE;
    } else {
        for (index = 4; index < MAX_GFX_SLOTS; index++) {
            if (gfxIndex == gGFXSlots.slots[index].gfxIndex) {
                // Gfx is already loaded to a slot.
                fprintf(stderr, "[GFX] LoadFixedGFX: gfxIndex=%u already in slot %u\n", gfxIndex, index);
                sub_080AE0C8(index, entity, GFX_SLOT_RESERVED);
                result = TRUE;
                return result;
            }
        }
        data = gFixedTypeGfxData[gfxIndex];
        fprintf(stderr, "[GFX] LoadFixedGFX: gfxIndex=%u data=0x%08X count=%u\n", gfxIndex, data,
                (data & 0x7f000000) >> 0x18);
        count = (data & 0x7f000000) >> 0x18;
        index = FindFreeGFXSlots(count);
        if (index != 0) {
            ReserveGFXSlots(index, gfxIndex, count);
            sub_080ADDD8(index, data);
        _080ADFF2:
            fprintf(stderr, "[GFX] LoadFixedGFX: assigned slot %u for gfxIndex=%u\n", index, gfxIndex);
            sub_080AE0C8(index, entity, GFX_SLOT_RESERVED);
            result = TRUE;
        } else {
            fprintf(stderr, "[GFX] LoadFixedGFX: NO FREE SLOTS for gfxIndex=%u (count=%u)\n", gfxIndex, count);
            result = FALSE;
        }
    }
    return result;
#else

    GfxSlot* slot;
    u32 index;
    u32 count;

    if (gfxIndex != 0) {
        for (index = 4; index < MAX_GFX_SLOTS; index++) {
            if (gfxIndex == gGFXSlots.slots[index].gfxIndex) {
                // Gfx is already loaded to a slot.
                fprintf(stderr, "[GFX] LoadFixedGFX: gfxIndex=%u already in slot %u\n", gfxIndex, index);
                goto _080ADFF2;
            }
        }
        count = (gFixedTypeGfxData[gfxIndex] & 0x7f000000) >> 0x18;
        fprintf(stderr, "[GFX] tick=%u LoadFixedGFX: gfxIndex=%u data=0x%08X count=%u\n", (u32)gMain.ticks, gfxIndex,
                gFixedTypeGfxData[gfxIndex], count);
        index = FindFreeGFXSlots(count);
        if (index == 0) {
            CleanUpGFXSlots();
            index = FindFreeGFXSlots(count);
            if (index == 0) {
                fprintf(stderr, "[GFX] LoadFixedGFX: NO FREE SLOTS for gfxIndex=%u (count=%u)\n", gfxIndex, count);
                return FALSE;
            }
        }
        ReserveGFXSlots(index, gfxIndex, count);
        sub_080ADDD8(index, gFixedTypeGfxData[gfxIndex]);
    _080ADFF2:
        fprintf(stderr, "[GFX] LoadFixedGFX: assigned slot %u for gfxIndex=%u\n", index, gfxIndex);
        sub_080AE0C8(index, entity, GFX_SLOT_RESERVED);
    }
    return TRUE;
#endif
}

// If slotIndex != 0 the gfx loaded starting from that slot, else in the first fitting free one.
bool32 LoadSwapGFX(Entity* entity, u32 count, u32 slotIndex) {
    u32 status;
    if ((slotIndex == 0) && (slotIndex = FindFreeGFXSlots(count), slotIndex == 0)) {
#ifndef EU
        CleanUpGFXSlots();
        slotIndex = FindFreeGFXSlots(count);
#endif
        if (slotIndex == 0) {
            goto _080AE058;
        }
    }
    status = gGFXSlots.slots[slotIndex].status;
    if (status != GFX_SLOT_PALETTE) {
        ReserveGFXSlots(slotIndex, 0, count);
        status = GFX_SLOT_GFX;
    }
    sub_080AE0C8(slotIndex, entity, status);
_080AE058:
    return slotIndex != 0;
}

void UnloadGFXSlots(Entity* param_1) {
    u32 slotIndex;
    GfxSlot* slot;
    s32 slotCount;

    slotIndex = param_1->spriteAnimation[0];
    param_1->spriteAnimation[0] = 0;
    if (slotIndex != 0) {
        slot = &gGFXSlots.slots[slotIndex];
        switch (slot->status) {
            case GFX_SLOT_RESERVED:
            case GFX_SLOT_GFX:
                if (slot->referenceCount != 0) {
                    if (--slot->referenceCount == 0) {
                        slotCount = slot->slotCount;
                        while (slotCount-- > 0) {
                            slot->status = GFX_SLOT_UNLOADED;
                            slot++;
                        }
                    }
                }
                break;
        }
    }
}

void sub_080AE0C8(u32 index, Entity* entity, u32 status) {
    GfxSlot* slot;
    entity->spriteVramOffset = index * 0x10 + 0x140;
    entity->spriteAnimation[0] = index;
    slot = &gGFXSlots.slots[index];
    if (*(s8*)&slot->referenceCount >= 0) {
        slot->referenceCount++;
    }
    SetGFXSlotStatus(slot, status);
}

void ReserveGFXSlots(u32 index, u32 gfxIndex, u32 slotCount) {
    GfxSlot* slot = &gGFXSlots.slots[index];
    MemClear(slot, slotCount * sizeof(GfxSlot));
    slot->slotCount = slotCount;
    slot->gfxIndex = gfxIndex;
    SetGFXSlotStatus(slot, GFX_SLOT_RESERVED);
}

void SetGFXSlotStatus(GfxSlot* slot, u32 status) {
    s32 index;
    slot->status = status;
    index = slot->slotCount;
    if (status != GFX_SLOT_PALETTE) {
        status = GFX_SLOT_FOLLOWER;
    }
    for (index--; index > 0; index--) {
        slot++;
        slot->status = status;
    }
}

/**
 * Finds slotCount continuos free slots and returns the index of the first slot or 0 if not enough free slots could be
 * found.
 */
u32 FindFreeGFXSlots(u32 slotCount) {
    u32 index;
    u32 continuosFreeSlots = 0;

    // First search for enough continuos free slots.
    for (index = 4; index < MAX_GFX_SLOTS; index++) {
        if (gGFXSlots.slots[index].status == GFX_SLOT_FREE) {
            continuosFreeSlots++;
            if (slotCount <= continuosFreeSlots) {
                return (index - continuosFreeSlots) + 1;
            }

        } else {
            continuosFreeSlots = 0;
        }
    }

    // Now also search for enough continuos free or unused slots.
    continuosFreeSlots = 0;
    index = 4;
    for (index = 4; index < MAX_GFX_SLOTS; index++) {
#ifdef EU
        if (gGFXSlots.slots[index].status == GFX_SLOT_UNLOADED) {
#else
        if (gGFXSlots.slots[index].status == GFX_SLOT_FREE || gGFXSlots.slots[index].status == GFX_SLOT_UNLOADED) {
#endif
            continuosFreeSlots++;
            if (slotCount <= continuosFreeSlots) {
                return (index - continuosFreeSlots) + 1;
            }
        } else {
            continuosFreeSlots = 0;
        }
    }
    return 0;
}

#ifndef EU
void CleanUpGFXSlots(void) {
    u32 occupiedIndex;
    u32 firstFreeIndex;
    if (gGFXSlots.unk0 != 0) {
        for (occupiedIndex = 4; (occupiedIndex = FindNextOccupiedGFXSlot(occupiedIndex)) != 0; occupiedIndex++) {
            firstFreeIndex = FindFirstFreeGFXSlot();
            if (firstFreeIndex <= occupiedIndex) {
                sub_080AE218(occupiedIndex, firstFreeIndex);
                MoveGFXSlots(occupiedIndex, firstFreeIndex);
                occupiedIndex = firstFreeIndex;
            }
        }
    }
}

// Swap gfx
void sub_080AE218(u32 param1, u32 param2) {
    struct_gUnk_020000C0_1* psVar6;
    u32 r0, r1, r3, r7, r12;
    u32 index1, index2;

    r12 = (param2 << 4) + 0x140;
    r3 = (param1 << 4) + 0x140;
    r7 = r3 + ((u32)gGFXSlots.slots[param1].slotCount << 4);

    for (index1 = 0; index1 < 0x50; index1++) {
        Entity* ent = (Entity*)&(&gPlayerEntity)[index1];
        if (ent->next != NULL) {
            if (param1 == ent->spriteAnimation[0]) {
                ent->spriteAnimation[0] = param2;
            }
            r0 = ent->spriteVramOffset;
            if ((r3 <= r0) && (r7 > r0)) {
                r0 = (r0 - r3);
                r1 = r0 + r12;
                ent->spriteVramOffset = r1;
            }
        }
    }

    for (index2 = 0; index2 < ARRAY_COUNT(gUnk_020000C0); index2++) {
        for (index1 = 0; index1 < 4; index1++) {
            psVar6 = gUnk_020000C0[index2].unk_00 + index1;
            if ((((*(u8*)&psVar6->unk_00) & 1) != 0) && (((*(u8*)&psVar6->unk_00) & 2) == 0)) {
                r1 = psVar6->unk_08.HALF_U.HI;
                if ((r3 <= r1) && (r7 > r1)) {
                    r0 = (r1 - r3);
                    r1 = r0 + r12;
                    psVar6->unk_08.HALF_U.HI = r1;
                }
            }
        }
    }

    for (index1 = 0; index1 < 0x80; index1++) {
        r1 = gOAMControls.oam[index1].tileNum;
        if ((r3 <= r1) && (r7 > r1)) {
            r0 = (r1 - r3);
            r1 = r0 + r12;
            gOAMControls.oam[index1].tileNum = r1;
            gOAMControls.field_0x0 = 1;
        }
    }
}

void MoveGFXSlots(u32 srcIndex, u32 targetIndex) {
    s32 index;
    u32 count;

    count = gGFXSlots.slots[srcIndex].slotCount;
    for (count = count - 1; count != -1; count--) {
        gGFXSlots.slots[targetIndex] = gGFXSlots.slots[srcIndex];
        MemClear(&gGFXSlots.slots[srcIndex], sizeof(GfxSlot));
        srcIndex++;
        targetIndex++;
    }
    gGFXSlots.unk_3 = 1;
}

u32 FindNextOccupiedGFXSlot(u32 index) {
    for (; index < MAX_GFX_SLOTS - 1; index++) {
        switch (gGFXSlots.slots[index].status) {
            case GFX_SLOT_RESERVED:
            case GFX_SLOT_GFX:
                return index;
        }
    }
    return 0;
}

u32 FindFirstFreeGFXSlot(void) {
    u32 index;
    for (index = 4; index < MAX_GFX_SLOTS; index++) {
        switch (gGFXSlots.slots[index].status) {
            case GFX_SLOT_FREE:
            case GFX_SLOT_UNLOADED:
                return index;
            default:
                break;
        }
    }
    return 0;
}
#endif
