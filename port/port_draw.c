/*
 * port_draw.c — C ports of IWRAM overlay drawing functions.
 *
 * The original game copies ARM assembly code to IWRAM at startup
 * (from ROM at sub_080B197C). These functions process sprite frame
 * data from gFrameObjLists and write OAM entries into gOAMControls.
 *
 * Ported functions:
 *   - ram_DrawDirect    (arm_DrawDirect @ 0x080B280C)
 *   - ram_sub_080ADA04  (arm_sub_080ADA04)
 *   - ram_DrawEntities  (arm_DrawEntities @ 0x080B23F0)
 *   - DrawEntity        (port of 0x0800404C)
 *   - CheckOnScreen     (port of 0x080040A8)
 *   - sub_080B2874      (core sprite piece renderer — static)
 */

#include "color.h"
#include "entity.h"
#include "global.h"
#include "room.h"
#include "screen.h"
#include "structures.h"

#include <string.h>

/* Forward declaration of RenderSpritePieces (defined below) */
static void RenderSpritePieces(const u8* data, s16 baseX, s16 baseY, u32 flags, u16 extra);

/* ---- Size/clipping table from ROM overlay (ram_0x80b2be8) ----
 * 60 entries × 4 bytes = 240 bytes.
 * Each entry: { xAnchor, yAnchor, xSize, ySize }
 * Sub-tables at offsets 0, 0x30, 0x60, 0x90, 0xC0 correspond to
 * different anchor modes (no anchor, x-anchor, y-anchor, xy-anchor,
 * double-size affine).
 */
static u8 sSizeTable[240];
static int sSizeTableLoaded = 0;

/* Called from port_rom.c after ROM is loaded */
void Port_LoadOverlayData(const u8* romData, u32 romSize) {
    /* Size table at ROM offset 0x0B2BE8, 240 bytes */
    if (romSize > 0x0B2BE8 + 240) {
        memcpy(sSizeTable, &romData[0x0B2BE8], 240);
        sSizeTableLoaded = 1;
    }
}

/* ====================================================================
 *  ram_UpdateEntities — Port of arm_UpdateEntities @ 0x080B21B0
 * ==================================================================== */

/* Entity update function declarations (implemented in respective .c files or stubs) */
extern void PlayerUpdate(Entity* entity);
extern void EnemyUpdate(Entity* entity);
extern void ProjectileUpdate(Entity* entity);
extern void ObjectUpdate(Entity* entity);
extern void NPCUpdate(Entity* entity);
extern void ItemUpdate(Entity* entity);
extern void ManagerUpdate(Entity* entity);
extern void DeleteThisEntity(void);

/* Update function table indexed by EntityKind (0-9) */
typedef void (*EntityUpdateFn)(Entity*);

static void DeleteThisEntityWrapper(Entity* unused) {
    DeleteThisEntity();
}

/* Initialized dynamically due to static init issues on Windows/MinGW */
static EntityUpdateFn sEntityUpdateFuncs[10] = { 0 };
static int sEntityUpdateFuncsInited = 0;

static void InitEntityUpdateFuncs(void) {
    if (sEntityUpdateFuncsInited)
        return;
    sEntityUpdateFuncs[0] = DeleteThisEntityWrapper; /* 0 = invalid */
    sEntityUpdateFuncs[1] = PlayerUpdate;            /* 1 = PLAYER */
    sEntityUpdateFuncs[2] = DeleteThisEntityWrapper; /* 2 = invalid */
    sEntityUpdateFuncs[3] = EnemyUpdate;             /* 3 = ENEMY */
    sEntityUpdateFuncs[4] = ProjectileUpdate;        /* 4 = PROJECTILE */
    sEntityUpdateFuncs[5] = DeleteThisEntityWrapper; /* 5 = invalid */
    sEntityUpdateFuncs[6] = ObjectUpdate;            /* 6 = OBJECT */
    sEntityUpdateFuncs[7] = NPCUpdate;               /* 7 = NPC */
    sEntityUpdateFuncs[8] = ItemUpdate;              /* 8 = PLAYER_ITEM */
    sEntityUpdateFuncs[9] = ManagerUpdate;           /* 9 = MANAGER */
    sEntityUpdateFuncsInited = 1;
}

/* UpdateContext matches the structure from entity.c */
typedef struct {
    void* table;
    void* list_top;
    Entity* current_entity;
    void* restore_sp;
} UpdateContext;

extern UpdateContext gUpdateContext;

/* External function called after each entity update */
extern void UpdateCollision(Entity* entity);

/* ram_UpdateEntities (port of arm_UpdateEntities)
 *
 * Arguments:
 *   mode = 0: update all entities (lists 0-7)
 *   mode = 1: update managers only (list 8)
 */
static int sUpdateEntitiesCalls = 0;
static int sEntitiesUpdated = 0;

void ram_UpdateEntities(u32 mode) {
    sUpdateEntitiesCalls++;

    /* Initialize function table on first call */
    InitEntityUpdateFuncs();

    int startList, endList;

    if (mode == 0) {
        /* Entities: lists 0-7 (excluding manager list) */
        startList = 0;
        endList = 8;
    } else {
        /* Managers: only list 8 */
        startList = 8;
        endList = 9;
    }

    for (int listIdx = startList; listIdx < endList; listIdx++) {
        LinkedList* list = &gEntityLists[listIdx];
        Entity* entity = list->first;

        while (entity != NULL && entity != (Entity*)list) {
            /* Save current entity in context */
            gUpdateContext.current_entity = entity;

            /* Get next before update (in case entity gets deleted) */
            Entity* next = entity->next;

            /* Call appropriate update function based on entity kind */
            u8 kind = entity->kind;

            sEntitiesUpdated++;

            if (kind < 10) {
                sEntityUpdateFuncs[kind](entity);
            }

            /* Update collision if entity is still alive (same as original) */
            if (gUpdateContext.current_entity == entity) {
                UpdateCollision(entity);
            }

            /* Move to next entity */
            entity = next;
        }
    }

    /* Clear current entity context */
    gUpdateContext.current_entity = NULL;
}

/* ---- Core sprite piece renderer (port of sub_080B2874) ----
 *
 * Frame data format:
 *   byte 0: piece count
 *   Then for each piece, 5 bytes:
 *     byte 0: signed x offset
 *     byte 1: signed y offset
 *     byte 2: shape/size/flags
 *       bits 7-6: OAM shape (0=square, 1=h-rect, 2=v-rect)
 *       bits 5-4: OAM size (0-3)
 *       bits 3-2: flip bits (XOR'd with base flags)
 *       bit 1: unused
 *       bit 0: if set, clear palette bits from attr2
 *     byte 3: tile index low byte
 *     byte 4: tile index high / palette addend
 */
static void RenderSpritePieces(const u8* data, /* pointer to frame data (count byte + pieces) */
                               s16 baseX,      /* OAMCommand.x */
                               s16 baseY,      /* OAMCommand.y */
                               u32 flags,      /* *(u32*)&cmd->_4 : affine/flip/mode flags */
                               u16 extra       /* cmd->_8 : base tile + priority + palette */
) {
    u8 count = *data++;
    if (count == 0)
        return;
    if (gOAMControls.updated >= 0x80)
        return;

    /* Determine size table sub-table offset based on flags */
    u32 tableOff;
    if (flags & 0x300) {
        /* Affine mode: bit 8 set */
        if ((flags & 0x300) != 0x100)
            tableOff = 0xC0; /* double-size affine */
        else
            tableOff = 0;
    } else {
        /* Normal mode: offset based on bits 28-29 */
        u32 v = (flags >> 24) & 0x30;
        tableOff = v + (v << 1); /* v * 3 */
    }
    const u8* sizeTab = &sSizeTable[tableOff];

    u8 updated = gOAMControls.updated;
    /* OAM entries start at offset 0x20 in OAMControls, each 8 bytes */
    u8* oamBase = (u8*)&gOAMControls.oam[0];
    u8* ip = oamBase + updated * 8;

    for (int i = 0; i < count; i++) {
        if (updated >= 0x80) {
            gOAMControls.updated = 0x80;
            return;
        }

        /* Read 5-byte piece */
        s8 xoff = (s8)data[0];
        s8 yoff = (s8)data[1];
        u8 shapeInfo = data[2];
        u8 tileLow = data[3];
        u8 tileHigh = data[4];
        data += 5;

        /* Apply flip if not in affine mode */
        if (!(flags & 0x300)) {
            if (flags & 0x20000000)
                yoff = -yoff; /* v-flip */
            if (flags & 0x10000000)
                xoff = -xoff; /* h-flip */
        }

        /* Screen position */
        s32 y = (s32)yoff + (s32)baseY;
        s32 x = (s32)xoff + (s32)baseX;

        /* Size table lookup for clipping */
        u32 sizeIdx = (shapeInfo & 0xF0) >> 2; /* 4-byte entries */
        const u8* se = sizeTab + sizeIdx;

        /* Clipping */
        y -= (s32)se[1]; /* subtract y anchor */
        if (y >= 160) {
            continue;
        }
        if (y + (s32)se[3] <= 0) {
            continue;
        }

        x -= (s32)se[0]; /* subtract x anchor */
        if (x >= 240) {
            continue;
        }
        if (x + (s32)se[2] <= 0) {
            continue;
        }

        /* Build combined attr0|attr1 as 32-bit word (little-endian):
         *   bits 0-7:   attr0.y
         *   bits 8-15:  attr0 flags (from 'flags' parameter)
         *   bits 14-15: attr0.shape
         *   bits 16-24: attr1.x
         *   bits 25-29: attr1.matrixNum/flip (from 'flags')
         *   bits 30-31: attr1.size
         */
        u32 oamWord = (u32)(y & 0xFF);            /* y position */
        oamWord |= (u32)((x & 0x1FF)) << 16;      /* x position */
        oamWord |= flags;                         /* base flags */
        oamWord |= (u32)(shapeInfo & 0xC0) << 8;  /* shape → attr0 bits 14-15 */
        oamWord ^= (u32)(shapeInfo & 0x3C) << 26; /* flip/size → attr1 bits 12-15 */

        *(u32*)ip = oamWord;
        ip += 4;

        /* Build attr2: tile number + priority + palette */
        u16 attr2 = (u16)tileLow + extra;
        if (shapeInfo & 1) {
            attr2 &= 0x0FFF; /* clear palette bits */
        }
        attr2 += (u16)tileHigh << 8;

        *(u16*)ip = attr2;
        ip += 4; /* skip affineParam (2 bytes attr2 + 2 bytes padding) */

        updated++;
    }

    gOAMControls.updated = updated;
}

/* ---- ram_DrawDirect (port of arm_DrawDirect @ 0x080B280C) ----
 *
 * Signature: void ram_DrawDirect(OAMCommand* cmd, u32 spriteIndex, u32 frameIndex)
 *
 * Looks up sprite frame data from gFrameObjLists using self-relative
 * offset chains, then renders sprite pieces into gOAMControls.oam[].
 */
extern u32 gFrameObjLists[];

void ram_DrawDirect(OAMCommand* cmd, u32 spriteIndex, u32 frameIndex) {
    if (frameIndex == 0xFF)
        return;

    /* Look up frame data using self-relative offsets:
     * 1. gFrameObjLists[spriteIndex] → byte offset to frame table
     * 2. At that offset + frameIndex*4, read another u32 → byte offset to data
     * 3. base + that offset → frame data pointer
     */
    u8* base = (u8*)gFrameObjLists;
    u32 off1 = gFrameObjLists[spriteIndex];           /* byte offset to frame table */
    u32 off2 = *(u32*)(base + off1 + frameIndex * 4); /* byte offset to frame data */
    const u8* frameData = base + off2;

    u8 count = frameData[0];
    if (count == 0)
        return;
    if (gOAMControls.updated >= 0x80)
        return;

    /* Extract OAMCommand fields */
    s16 baseX = cmd->x;
    s16 baseY = cmd->y;
    u32 cmdFlags = *(u32*)&cmd->_4; /* _4 | (_6 << 16) */
    u16 cmdExtra = cmd->_8;

    RenderSpritePieces(frameData, baseX, baseY, cmdFlags, cmdExtra);
}

/* ---- ram_sub_080ADA04 (port of arm_sub_080ADA04) ----
 *
 * Signature: void ram_sub_080ADA04(OAMCommand* cmd, void* frameData)
 *
 * Same as DrawDirect but takes a pre-resolved frame data pointer
 * instead of looking it up from gFrameObjLists.
 */
void ram_sub_080ADA04(OAMCommand* cmd, void* frameDataPtr) {
    const u8* frameData = (const u8*)frameDataPtr;
    u8 count = frameData[0];
    if (count == 0)
        return;
    if (gOAMControls.updated >= 0x80)
        return;

    s16 baseX = cmd->x;
    s16 baseY = cmd->y;
    u32 cmdFlags = *(u32*)&cmd->_4;
    u16 cmdExtra = cmd->_8;

    RenderSpritePieces(frameData, baseX, baseY, cmdFlags, cmdExtra);
}

/* ====================================================================
 *  ENTITY DRAW LISTS — PC replacement for gUnk_081326EC IWRAM buffers
 * ==================================================================== */

/*
 * On GBA, gUnk_081326EC holds 5 IWRAM pointers to draw list buffers.
 * On PC those addresses are meaningless, so we allocate our own buffers.
 *
 * Layout of each list (matches GBA memory layout):
 *   byte 0:        count (max 64)
 *   bytes 1-3:     padding
 *   bytes 4..259:  up to 64 entity pointers (4 bytes each on GBA / ptr-sized on PC)
 */
typedef struct {
    u8 count;
    u8 _pad[3];
    Entity* entries[64];
} EntityDrawList;

/* 4 main draw lists + 1 deferred (shadow) list */
static EntityDrawList sDrawLists[5];

/* Deferred draw entry (packed as on GBA: two shorts per entry) */
typedef struct {
    s16 packed0; /* (screenX << 6) | shadowType */
    s16 packed1; /* (screenY << 6) | priority   */
} DeferredEntry;

typedef struct {
    u8 count;
    u8 _pad[3];
    DeferredEntry entries[64];
} DeferredDrawList;

static DeferredDrawList sDeferredList;

/* ---- CheckOnScreen (port of ASM at 0x080040A8) ---- */
u32 CheckOnScreen(Entity* entity) {
    s32 x = (s32)entity->x.HALF.HI - (s32)gRoomControls.scroll_x;
    x += 0x3F;
    if ((u32)x >= 0x16E)
        return 0;

    s32 y = (s32)entity->y.HALF.HI - (s32)gRoomControls.scroll_y;
    y += (s32)entity->z.HALF.HI;
    y += 0x3F;
    if ((u32)y >= 0x11E)
        return 0;

    return 1;
}

/* ---- DrawEntity (port of ASM at 0x0800404C) ----
 *
 * Called from ObjectUpdate / NpcUpdate / EnemyUpdate / etc.
 * Checks visibility, then adds the entity to the appropriate draw list.
 */
extern u8 gUnk_02024048; /* pending sound count */

void DrawEntity(Entity* entity) {
    u8 draw = *(u8*)&entity->spriteSettings & 3; /* bits 0-1 */
    if (draw == 0) {
        gUnk_02024048 = 0;
        return;
    }

    if (draw != 3) {
        /* sub_080040A2: check bit 1 of spriteSettings byte */
        u8 rawSS = *(u8*)&entity->spriteSettings;
        if (!(rawSS & 2)) {
            /* Not "draw always" — check on-screen */
            if (!CheckOnScreen(entity)) {
                gUnk_02024048 = 0;
                return;
            }
        }
        /* else bit 1 set (draw==2 or draw with ss2), treat as on-screen */
    }

    /* Determine draw list index from spriteRendering.b3 (bits 6-7 of byte 0x19) */
    u8 rawSR = *(u8*)&entity->spriteRendering;
    u32 listIdx = (rawSR & 0xC0) >> 6;
    EntityDrawList* list = &sDrawLists[listIdx];

    if (list->count < 64) {
        list->entries[list->count] = entity;
        list->count++;
    }

    /* Clear pending sound counter (sound is handled elsewhere on PC) */
    gUnk_02024048 = 0;
}

/* ---- ResolveOamDrawPriority (port of ASM at 0x080B2478) ----
 *
 * Sorts entity pointers in a draw list by (y-position + priority).
 * Uses shell sort with halving gap sequence, matching the original
 * ARM implementation.
 */
static void ResolveOamDrawPriority(EntityDrawList* list) {
    u32 n = list->count;
    if (n <= 1)
        return;

    u32 gap = n - 1;
    while (gap > 0) {
        for (u32 i = 0; i < n - gap; /* empty */) {
            u32 j = i;
            while (1) {
                u32 k = j + gap;
                if (k >= n)
                    break;
                Entity* a = list->entries[j];
                Entity* b = list->entries[k];
                /* Priority key from ASM:
                 * pos = (entity->y.WORD + 0x80000000) >> 3
                 * prio = ~entity->spritePriority.raw (inverted so 0 = highest)
                 * key = pos | (prio << 29)
                 * Higher key = draw LATER (lower draw priority).
                 * Swap if a's key < b's key (a should draw after b).
                 */
                u32 keyA = ((u32)(a->y.WORD + 0x80000000U) >> 3) | ((u32)(~(*(u8*)&a->spritePriority)) << 29);
                u32 keyB = ((u32)(b->y.WORD + 0x80000000U) >> 3) | ((u32)(~(*(u8*)&b->spritePriority)) << 29);
                if (keyA >= keyB)
                    break; /* a has equal or higher key — correct order */
                /* Shift b down, insert a at k */
                list->entries[k] = a;
                list->entries[j] = b;
                if (j < gap)
                    break;
                j -= gap;
            }
            i++;
        }
        gap >>= 1;
    }
}

/* ---- ResolveEntitySpriteParams (port of sub_080B299C) ----
 *
 * Computes drawing parameters from entity fields:
 *   screenX, screenY, flags (attr0/attr1 bits), extra (attr2 bits)
 */
static void ResolveEntitySpriteParams(Entity* entity, s32* outX, s32* outY, u32* outFlags, u16* outExtra) {
    /* Iframes palette override */
    u8 iframesPal = 0;
    if (entity->iframes > 0) {
        iframesPal = ((u8*)&gOAMControls)[0x0E]; /* gOAMControls._0[6] */
    }

    /* Load 32-bit word at offset 0x18: spriteSettings|spriteRendering|palette|spriteOrientation */
    u32 ip = *(u32*)&entity->spriteSettings;

    /* Build attr2 base (extra): tile + priority + palette */
    u16 sb = entity->spriteVramOffset;
    u8 palRaw = entity->palette.raw;
    palRaw |= iframesPal;
    palRaw &= 0x0F;
    sb |= (u16)palRaw << 12;

    u8 orientRaw = *(u8*)&entity->spriteOrientation;
    sb |= (u16)(orientRaw & 0xC0) << 4; /* priority bits → attr2 bits 10-11 */

    /* Screen position */
    s32 x = (s32)entity->x.HALF.HI + (s32)(s8)entity->spriteOffsetX;
    s32 y = (s32)entity->y.HALF.HI + (s32)entity->z.HALF.HI + (s32)entity->spriteOffsetY;

    /* Scroll subtraction (skip for draw mode 2) */
    if ((ip & 3) != 2) {
        x -= (s16)gOAMControls._4;
        y -= (s16)gOAMControls._6;
    }

    /* Build flags word */
    u32 r8 = ip & 0x3E003F00U;
    r8 |= (u32)gOAMControls.spritesOffset << 12;

    u8 frameSS = entity->frameSpriteSettings;
    u8 ssRaw = *(u8*)&entity->spriteSettings;
    u8 flipBits = (frameSS ^ ssRaw) & 0xC0;
    r8 |= (u32)flipBits << 22; /* bits 28-29 */

    *outX = x;
    *outY = y;
    *outFlags = r8;
    *outExtra = sb;
}

/* ---- LookupAndRenderNormal (port of _080B27E4 + sub_080B27F4) ---- */

static void LookupAndRenderNormal(Entity* entity, s32 x, s32 y, u32 flags, u16 extra) {
    u8 frameIdx = entity->frameIndex;
    if (frameIdx == 0xFF)
        return;

    u16 sprIdx = (u16)entity->spriteIndex;
    u8* base = (u8*)gFrameObjLists;
    u32 off1 = gFrameObjLists[sprIdx];
    u32 off2 = *(u32*)(base + off1 + (u32)frameIdx * 4);
    const u8* frameData = base + off2;

    RenderSpritePieces(frameData, (s16)x, (s16)y, flags, extra);
}

/* ---- DrawEntitySprites (port of _080B2718 + sub-paths) ----
 *
 * Handles the three entity rendering modes:
 *   spriteAnimation[2] == 0: normal sprite via gFrameObjLists
 *   spriteAnimation[2] < 0:  direct frame data from entity->myHeap
 *   spriteAnimation[2] > 0:  multi-part from gUnk_020000C0
 */
static void DrawEntitySprites(Entity* entity, s32 x, s32 y, u32 flags, u16 extra) {
    s8 renderMode = (s8)entity->spriteAnimation[2]; /* offset 0x28 */

    if (renderMode == 0) {
        /* Normal sprite rendering */
        LookupAndRenderNormal(entity, x, y, flags, extra);
    } else if (renderMode < 0) {
        /* Direct frame data from myHeap */
        const u8* frameData = (const u8*)entity->myHeap;
        if (frameData) {
            RenderSpritePieces(frameData, (s16)x, (s16)y, flags, extra);
        }
    } else {
        /* Multi-part from gUnk_020000C0 */
        struct_gUnk_020000C0* slot = &gUnk_020000C0[(u8)renderMode];
        u16 cleanExtra = extra & 0x0FFF; /* clear palette bits */

        for (int part = 0; part < 4; part++) {
            struct_gUnk_020000C0_1* sub = &slot->unk_00[part];
            u8 subFlags = sub->unk_00.unk0; /* bit 0-1 flags (raw struct) */

            if (!(*(u8*)&sub->unk_00 & 1)) /* not active */
                return;

            if (*(u8*)&sub->unk_00 & 2) {
                /* Sub-entity mode — entity pointer at offset 0x0C */
                Entity* subEntity = (Entity*)(uintptr_t)sub->unk_0C;
                if (subEntity) {
                    s32 sx, sy;
                    u32 sf;
                    u16 se;
                    ResolveEntitySpriteParams(subEntity, &sx, &sy, &sf, &se);
                    u8 extraYOff = ((u8*)&gOAMControls)[0x12]; /* fp[0x12] */
                    sy += extraYOff;
                    LookupAndRenderNormal(subEntity, sx, sy, sf, se);
                }
            } else {
                /* Direct sub-sprite */
                u8 subSprSlot = sub->unk_01;
                if (subSprSlot == 0xFF)
                    continue; /* skip to next part (don't return!) */

                /* Apply sub-part overrides */
                u32 partFlags = flags;
                s32 partX = x;
                s32 partY = y;
                u16 partExtra = cleanExtra;

                u8 flipOverride = (u8)((sub->unk_02 >> 8) & 0xFF); /* byte at +4 in GBA layout */
                partFlags ^= (u32)flipOverride << 28;

                s8 xOff = (s8)((sub->unk_04.WORD >> 16) & 0xFF); /* byte at +6 */
                s8 yOff = (s8)((sub->unk_04.WORD >> 24) & 0xFF); /* byte at +7 */

                if (!(partFlags & 0x10000000))
                    partX += xOff;
                else
                    partX -= xOff;
                if (!(partFlags & 0x20000000))
                    partY += yOff;
                else
                    partY += yOff; /* Note: GBA uses ADD for both vflip cases */

                u8 palBits = (u8)((sub->unk_04.WORD >> 8) & 0xFF); /* byte at +5 */
                partExtra |= (u16)palBits << 12;

                u8 tileOff = (u8)(sub->unk_08.WORD & 0xFF); /* byte at +8 */
                partExtra += tileOff;

                u16 sprIdx = sub->unk_02 & 0xFFFF;

                /* Look up and render */
                u8 fIdx = entity->frameIndex;
                if (fIdx != 0xFF) {
                    u8* base = (u8*)gFrameObjLists;
                    u32 off1 = gFrameObjLists[sprIdx];
                    u32 off2 = *(u32*)(base + off1 + (u32)fIdx * 4);
                    const u8* frameData = base + off2;
                    RenderSpritePieces(frameData, (s16)partX, (s16)partY, partFlags, partExtra);
                }
            }
        }
    }
}

/* ---- ProcessEntityForDraw (port of sub_080B255C, USA path) ----
 *
 * Handles one entity: resolves params, checks shadow flags, renders.
 */
static void ProcessEntityForDraw(Entity* entity) {
    s32 x, y;
    u32 flags;
    u16 extra;

    ResolveEntitySpriteParams(entity, &x, &y, &flags, &extra);

    /* Check shadow flag (bit 3 of spritePriority byte, offset 0x29) */
    s8 prioRaw = *(s8*)&entity->spritePriority;
    if (!(prioRaw & 8)) {
        /* No shadow — just draw the entity sprites */
        DrawEntitySprites(entity, x, y, flags, extra);
        return;
    }

    /* Has shadow flag — check z for shadow rendering */
    s16 zVal = entity->z.HALF.HI;
    if (zVal < 0) {
        /* z < 0: skip shadow sprite, just draw entity */
        DrawEntitySprites(entity, x, y, flags, extra);
    } else {
        /* z >= 0: would draw shadow sprite first, then entity.
         * Shadow rendering uses ram_0x80b2b58 lookup tables which
         * aren't loaded yet. For now, just draw the entity. */
        DrawEntitySprites(entity, x, y, flags, extra);
    }

    /* Deferred list handling — add shadow/underlay entry */
    u8 prioByte = *(u8*)&entity->spritePriority;
    if (prioByte & 0x10) {
        if (zVal >= 0)
            return; /* skip */
    }
    if (prioByte & 0x20) {
        if (gOAMControls.field_0x1 & 1)
            return;
    }

    s32 deferY = y;
    if (zVal < 0)
        deferY -= zVal;

    if (sDeferredList.count >= 64)
        return;
    sDeferredList.count++;
    DeferredEntry* de = &sDeferredList.entries[sDeferredList.count - 1];
    u16 prioBits = (extra >> 10) & 3;
    de->packed1 = (s16)((deferY << 6) | prioBits);
    u8 shadowType = (*(u8*)&entity->spriteSettings & 0x30) >> 4;
    de->packed0 = (s16)((x << 6) | shadowType);
}

/* ---- ProcessDrawList (port of sub_080B2534) ---- */
static void ProcessDrawList(EntityDrawList* list) {
    for (u32 i = 0; i < list->count; i++) {
        if (gOAMControls.updated >= 0x80)
            return;
        ProcessEntityForDraw(list->entries[i]);
    }
}

/* ---- ProcessDeferredList (port of _080B26B4) ----
 *
 * Renders shadow/underlay sprites from the deferred list.
 * Uses ram_0x80b2bd8 (shadow frame data table) which needs
 * to be loaded from ROM overlay data.
 */
static u8 sShadowFrameTable[16 * 4]; /* ram_0x80b2bd8: 4 pointers (frame data) */
static int sShadowTableLoaded = 0;

static void ProcessDeferredList(void) {
    if (sDeferredList.count == 0)
        return;
    if (!sShadowTableLoaded)
        return; /* Can't render shadows without table */

    for (u32 i = 0; i < sDeferredList.count; i++) {
        if (gOAMControls.updated >= 0x80)
            return;
        DeferredEntry* de = &sDeferredList.entries[i];
        s32 screenX = de->packed0 >> 6;
        u32 listType = de->packed0 & 7;
        s32 screenY = de->packed1 >> 6;
        u32 prioBits = de->packed1 & 3;
        u16 extra = (u16)(prioBits << 10);

        u32* shadowPtrs = (u32*)sShadowFrameTable;
        const u8* frameData = (const u8*)(uintptr_t)shadowPtrs[listType];
        if (frameData == NULL)
            continue;

        RenderSpritePieces(frameData, (s16)screenX, (s16)screenY, 0, extra);
    }
}

/* ---- ram_DrawEntities (port of arm_DrawEntities @ 0x080B23F0) ----
 *
 * Main entry point: iterates the 4 entity draw lists, sorts by
 * priority, renders each entity's sprites into gOAMControls.oam[].
 */

void ram_DrawEntities(void) {
    if (gOAMControls.updated >= 0x80)
        return;

    for (int i = 0; i < 4; i++) {
        EntityDrawList* list = &sDrawLists[i];
        if (list->count == 0)
            continue;

        /* Clear deferred list for this batch */
        sDeferredList.count = 0;

        /* Sort entities by y + priority */
        ResolveOamDrawPriority(list);

        /* Process each entity */
        ProcessDrawList(list);

        /* Render deferred shadow/underlay sprites */
        ProcessDeferredList();

        /* Clear draw list for next frame */
        list->count = 0;
    }
}
