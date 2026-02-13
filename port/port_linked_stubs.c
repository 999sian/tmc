
#include "area.h"
#include "backgroundAnimations.h"
#include "beanstalkSubtask.h"
#include "collision.h"
#include "color.h"
#include "common.h"
#include "entity.h"
#include "fade.h"

#define gMapDataBottomSpecial gMapDataBottomSpecial_HIDDEN
#include "fileselect.h"
#undef gMapDataBottomSpecial
#include "hitbox.h"
#include "kinstone.h"
#include "main.h"
#include "map.h"
#include "menu.h"
#include "message.h"
#include "npc.h"
#include "player.h"
#include "room.h"
#include "save.h"
#include "screen.h"
#include "script.h"
#include "sound.h"
#include "structures.h"

#include "port_entity_ctx.h"
#include "port_gba_mem.h"

uint16_t gPortIntrCheck;
void* gPortIntrVector;
struct SoundInfo* gPortSoundInfoPtr;

// Data globals
GfxSlotList gGFXSlots;
Message gMessage;
TextRender gTextRender;
u16 gPaletteBuffer[0x200];
Input gInput;
u32 gRand;
Screen gScreen;
OAMCommand gOamCmd;
Main gMain;
FadeControl gFadeControl;
OAMControls gOAMControls;
SoundPlayingInfo gSoundPlayingInfo;

u32 gUsedPalettes;

// Pointers
struct_02000010 gUnk_02000010;
u8 gUnk_02000030[0x10]; /* EWRAM marker, 16 bytes gap */
struct_02000040 gUnk_02000040;
void* gUnk_020000B0 = NULL; /* Entity* pointer (8 bytes on 64-bit) */
struct_gUnk_020000C0 gUnk_020000C0[0x30];
Palette gUnk_02001A3C;
u8 gUnk_02006F00[0x4000] __attribute__((aligned(4))); /* BG tilemap buffer (16 KB) */
u16 gUnk_0200B640;                                    /* scroll state scalar */
u16 gUnk_02017830[0x138] __attribute__((aligned(4))); /* palette rotation buffer (624 bytes) */
u8 gUnk_02017AA0[0x1400] __attribute__((aligned(4))); /* HBlank DMA double buffer, 2×0xA00 */
u8 gUnk_02017BA0[0x1400]
    __attribute__((aligned(4))); /* BG2 affine ref lines (TODO: aliases gUnk_02017AA0+0x100 on GBA) */
void* gUnk_02018EA0 = NULL;      /* LinkedList2* pointer */
struct_02018EB0 gUnk_02018EB0;
u8 gUnk_02018EE0[0x1000] __attribute__((aligned(4))); /* window rasterization scratch (s16[], 0x780 used, 0x1000 gap) */
u16 gUnk_02021F00[0x10];                              /* lever timer→length map (32 bytes) */
u8 gUnk_020227DC[0xC];                                /* text number buffer slot 0 */
struct_020227E8 gUnk_020227E8[1];                     /* text struct (8 bytes, was [16] = overflow) */
u8 gUnk_020227F0[0x8];                                /* text number buffer slot 2 */
u8 gUnk_020227F8[0x8];                                /* text number buffer slot 3 */
u8 gUnk_02022800[0x20] __attribute__((aligned(4)));   /* text number buffer slot 4 (29+ bytes) */
u8 gUnk_02022830[0x1800] __attribute__((aligned(4))); /* u16[0xc00] on GBA; also reused as temp MapDataDefinition */
u8 gUnk_02024048 = 0;                                 /* pending sound count (used by DrawEntity) */
u8 gUnk_020246B0[0x1800] __attribute__((aligned(4))); /* u16[0xc00] scroll tilemap buffer */
u8 gUnk_02033290[0xC00] __attribute__((aligned(8)));  /* Manager pool: 32 Temp structs (96 bytes each on 64-bit) */
u8 gUnk_020342F8[0x100] __attribute__((aligned(4)));  /* delayedEntityLoad array */
u8 gUnk_02034330[0x20] __attribute__((aligned(4)));   /* struct_gUnk_02034330 (24 bytes) */
struct_02034480 gUnk_02034480;
u8 gUnk_02034492[0x10] __attribute__((aligned(4))); /* u8 array (pauseMenu) */
u8 gUnk_020344A0[0x10] __attribute__((aligned(4))); /* u8[8] (figurineMenu) */
struct_020354C0 gUnk_020354C0[32];
u8 gUnk_02035542[0x200] __attribute__((aligned(4))); /* u8 array (entity tile storage) */
u8 gUnk_02036540[0x80] __attribute__((aligned(8)));  /* WStruct[4] (text slot pool, 64 bytes on 64-bit) */
/* Aliased to gEwram[0x36A58] in text.c */
u32 gUnk_02036A58_storage = 0x02036A58;
/* Aliased to gEwram[0x36AD8] in text.c */
u32 gUnk_02036AD8_storage = 0x02036AD8;
u32 gUnk_02036BB8 = 0x02036BB8;
// ========== Global data ==========

// Core systems
UI gUI;
HUD gHUD;
// gMenu / gChooseFileState / gIntroState share memory (EWRAM 0x02000080)
u8 _gMenuSharedStorage[0x40] __attribute__((aligned(8))) = { 0 };
Area gArea;
SaveFile gSave;
VBlankDMA gVBlankDMA;
u8 gUpdateVisibleTiles;

// Player
PlayerEntity gPlayerEntity;
PlayerState gPlayerState;
Entity* gPlayerClones[3];
ScriptExecutionContext gPlayerScriptExecutionContext;

// Entities
GenericEntity gEntities[MAX_ENTITIES];
GenericEntity gAuxPlayerEntities[MAX_AUX_PLAYER_ENTITIES];
LinkedList gEntityLists[9];
LinkedList gEntityListsBackup[9];
u8 gEntCount;
u8 gManagerCount;

// Side table for 64-bit ScriptExecutionContext pointers
// (avoids overflowing PlayerEntity's 4-byte unk_84 field with 8-byte pointers)
ScriptExecutionContext* gEntityScriptCtxTable[PC_MAX_ENTITY_SLOTS] = { 0 };
CarriedEntity gCarriedEntity;
ItemBehavior gActiveItems[MAX_ACTIVE_ITEMS];
PriorityHandler gPriorityHandler;
PossibleInteraction gPossibleInteraction;

// Room / Map
RoomControls gRoomControls;
MapLayer gMapBottom;
MapLayer gMapTop;
RoomTransition gRoomTransition;
RoomVars gRoomVars;
RoomMemory gRoomMemory[4];
RoomMemory* gCurrentRoomMemory;
void** gCurrentRoomProperties;

// Script
ActiveScriptInfo gActiveScriptInfo;
ScriptExecutionContext gScriptExecutionContextArray[0x20];

// Misc
BgAnimation gBgAnimations[MAX_BG_ANIMATIONS];
u8 gTextGfxBuffer[0xD00];
u8 gPaletteBufferBackup[0x400];
u8 gCollidableCount;

// gFrameObjLists — sprite frame data (200KB, self-relative offsets)
u32 gFrameObjLists[50016];

// gMapData — map data blob, backed by ROM data.
// On GBA this is a label in .rodata at gAreaRoomMap_None (~14MB region).
// On PC, we use a large buffer filled from ROM in Port_LoadRom().
// Source files use &gMapData + offset, so this must be an array (not a pointer).
u8 gMapData[0xE00000] __attribute__((aligned(4))); /* ~14 MB */

// gCollisionMtx
u8 gCollisionMtx[173 * 34];

u8 gUpdateContext[64] __attribute__((aligned(4)));

u8 gInteractableObjects[0x300]
    __attribute__((aligned(8))); /* 32 InteractableObject entries * 24 bytes on 64-bit = 768 = 0x300 */

// === Additional data stubs ===

Palette gPaletteList[0x10];

const Hitbox gHitbox_0, gHitbox_1, gHitbox_2, gHitbox_3, gHitbox_4, gHitbox_5;
const Hitbox gHitbox_6, gHitbox_7, gHitbox_8, gHitbox_9, gHitbox_10, gHitbox_11;
const Hitbox gHitbox_12, gHitbox_13, gHitbox_14, gHitbox_15, gHitbox_16, gHitbox_17;
const Hitbox gHitbox_18, gHitbox_20, gHitbox_21, gHitbox_22, gHitbox_23, gHitbox_24;
const Hitbox gHitbox_27, gHitbox_29, gHitbox_30, gHitbox_31, gHitbox_32;
const Hitbox3D gHitbox_19, gHitbox_25, gHitbox_26, gHitbox_28;

// NPC data
NPCStruct gNPCData[50];

/* ---------- Townsperson NPC function pointer tables ---------- */
/* These were .4byte tables in ROM pointing to Thumb function addresses.
 * On PC we define them as proper C function pointer arrays. */
extern void sub_08061BC8(Entity*);
extern void sub_08061C00(Entity*);
extern void sub_08061CEC(/* TownspersonEntity* */ Entity*);
extern void sub_08061D64(/* TownspersonEntity* */ Entity*);
extern void sub_08061E24(/* TownspersonEntity* */ Entity*);
extern void sub_08061E50(/* TownspersonEntity* */ Entity*);

void (*const gUnk_0810B774[])(Entity*) = {
    sub_08061BC8,
    sub_08061C00,
};
void (*const gUnk_0810B77C[])(Entity*) = {
    sub_08061CEC,
    sub_08061D64,
    sub_08061E24,
    sub_08061E50,
};

/* ---------- Pause menu / subtask function pointer tables ---------- */
/* From data/const/subtask.s — .4byte tables referencing C functions. */

/* PauseMenu_Screen_10 dispatch (pauseMenu.c) */
extern void sub_080A59AC(void);
extern void sub_080A59C8(void);
extern void sub_080A5A54(void);
extern void sub_080A5A90(void);
void (*const gUnk_08128D14[])(void) = {
    sub_080A59AC,
    sub_080A59C8,
    sub_080A5A54,
    sub_080A5A90,
};

/* PauseMenu_Screen_9 dispatch (pauseMenu.c) */
extern void sub_080A5AF4(void);
extern void sub_080A5B34(void);
extern void sub_080A5BB8(void);
void (*const gUnk_08128D24[])(void) = {
    sub_080A5AF4,
    sub_080A5B34,
    sub_080A5BB8,
};

/* PauseMenu_Screen_5 dispatch (pauseMenu.c) */
extern void sub_080A5C44(u32, u32, u32); /* actual signature, but called as void(void) from dispatch */
extern void sub_080A5C9C(void);
void (*const gUnk_08128D30[])(void) = {
    (void (*)(void))sub_080A5C44,
    sub_080A5C9C,
};

/* PauseMenu_Screen_7 dispatch (pauseMenu.c) */
extern void sub_080A6024(void);
extern void sub_080A6044(void);
void (*const gUnk_08128D58[])(void) = {
    sub_080A6024,
    sub_080A6044,
};

/* PauseMenu_Screen_8 dispatch (pauseMenu.c) */
extern void sub_080A6108(void);
extern void sub_080A612C(void);
void (*const gUnk_08128DB0[])(void) = {
    sub_080A6108,
    sub_080A612C,
};

/* PauseMenu_Screen_4 dispatch (pauseMenu.c) */
extern void sub_080A6290(void);
extern void sub_080A62E0(void);
void (*const gUnk_08128DCC[])(void) = {
    sub_080A6290,
    sub_080A62E0,
};

/* PauseMenu_Screen_6 dispatch (pauseMenuScreen6.c) */
extern void sub_080A6650(void);
extern void sub_080A667C(void);
void (*const gUnk_08128E78[])(void) = {
    sub_080A6650,
    sub_080A667C,
};

/* Subtask local map hint dispatch (subtaskLocalMapHint.c) */
extern void sub_080A6B04(void);
extern void sub_080A6C1C(void);
void (*const gUnk_08128F1C[])(void) = {
    sub_080A6B04,
    sub_080A6C1C,
};

// Pause menu
PauseMenuOptions gPauseMenuOptions;

SpritePtr gSpritePtrs[512];

// Map data — both are u16[0x4000] tilemaps (0x8000 bytes) reused for file-select overlay
u16 gMapDataBottomSpecial[0x4000] __attribute__((aligned(4)));
u16 gMapDataTopSpecial[0x4000] __attribute__((aligned(4)));
u32 gDungeonMap[0x800];

// Heap
u8 gzHeap[0x1000];

// Kinstone fusion
FuseInfo gFuseInfo;

// Room / tile entities
TileEntity gSmallChests[8];
SpecialTileEntry gTilesForSpecialTiles[MAX_SPECIAL_TILES];

// Collision
u8 gUnk_03003C70[16 * 20] __attribute__((aligned(4)));

// IWRAM scratch
u8 gUnk_03000420[0x800] __attribute__((aligned(4)));
u8 gUnk_03000C30;
u8 gUnk_03001020[sizeof(Screen)] __attribute__((aligned(4)));

// Sound player data
u8 gMPlayInfos[0x1C * 0x50] __attribute__((aligned(4)));
u8 gMPlayInfos2[0x4 * 0x50] __attribute__((aligned(4)));
u8 gMPlayTracks[0x50 * 16] __attribute__((aligned(4)));

// BGM song headers (ROM data stubs)
u8 bgmBeanstalk[0x10], bgmBeatVaati[0x10], bgmBossTheme[0x10], bgmCastleCollapse[0x10];
u8 bgmCastleMotif[0x10], bgmCastleTournament[0x10], bgmCastorWilds[0x10], bgmCaveOfFlames[0x10];
u8 bgmCloudTops[0x10], bgmCredits[0x10], bgmCrenelStorm[0x10], bgmCuccoMinigame[0x10];
u8 bgmDarkHyruleCastle[0x10], bgmDeepwoodShrine[0x10], bgmDiggingCave[0x10], bgmDungeon[0x10];
u8 bgmElementGet[0x10], bgmElementTheme[0x10], bgmElementalSanctuary[0x10], bgmEzloGet[0x10];
u8 bgmEzloStory[0x10], bgmEzloTheme[0x10], bgmFairyFountain[0x10], bgmFairyFountain2[0x10];
u8 bgmFestivalApproach[0x10], bgmFightTheme[0x10], bgmFightTheme2[0x10], bgmFileSelect[0x10];
u8 bgmFortressOfWinds[0x10], bgmGameover[0x10], bgmHouse[0x10], bgmHyruleCastle[0x10];
u8 bgmHyruleCastleNointro[0x10], bgmHyruleField[0x10], bgmHyruleTown[0x10], bgmIntroCutscene[0x10];
u8 bgmLearnScroll[0x10], bgmLostWoods[0x10], bgmLttpTitle[0x10], bgmMinishCap[0x10];
u8 bgmMinishVillage[0x10], bgmMinishWoods[0x10], bgmMtCrenel[0x10], bgmPalaceOfWinds[0x10];
u8 bgmPicoriFestival[0x10], bgmRoyalCrypt[0x10], bgmRoyalValley[0x10], bgmSavingZelda[0x10];
u8 bgmSecretCastleEntrance[0x10], bgmStory[0x10], bgmSwiftbladeDojo[0x10], bgmSyrupTheme[0x10];
u8 bgmTempleOfDroplets[0x10], bgmTitleScreen[0x10], bgmUnused[0x10], bgmVaatiMotif[0x10];
u8 bgmVaatiReborn[0x10], bgmVaatiTheme[0x10], bgmVaatiTransfigured[0x10], bgmVaatiWrath[0x10];
u8 bgmWindRuins[0x10];

// Linker symbols (unused on PC)
u8 RAMFUNCS_END[4];
u8 gCopyToEndOfEwram_End[4];
u8 gCopyToEndOfEwram_Start[4];
u8 gEndOfEwram[4];
u8 sub_080B197C[4];
u8 ram_sub_080B197C[4];
u32 ram_MakeFadeBuff256;

/*
 * C reimplementation of ram_sub_080B197C (ARM IWRAM function).
 * Called when gUpdateVisibleTiles == 1 (initial full-screen tile fill).
 * Copies a 32×23 tile region from mapSpecial to the BG buffer.
 *
 * mapSpecial: u16 tilemap with 128 entries per row (8×8 pixel tiles).
 * bgBuffer:   passed as gBGxBuffer + 0x20 (u16 units, +0x40 bytes).
 *             The GBA code subtracts 0x40 bytes to start writing from row 0.
 */
static void ram_sub_080B197C_c(u16* mapSpecial, u16* bgBuffer) {
    u16 xdiff = gRoomControls.scroll_x - gRoomControls.origin_x;
    u16 ydiff = gRoomControls.scroll_y - gRoomControls.origin_y;

    /* Tile position in 16×16 units → each maps to 2×2 sub-tiles of 8×8.
     * Byte offset = (col16 + row16 * 128) * 4, because each 16×16 tile
     * is 2 sub-tile columns (×2 bytes each = 4 bytes) and each 16×16 row
     * spans 2 sub-tile rows (128 entries × 2 bytes × 2 = 512 bytes). */
    u32 col16 = xdiff >> 4;
    u32 row16 = ydiff >> 4;
    u8* src = (u8*)mapSpecial + (col16 + row16 * 128) * 4;

    /* bgBuffer was passed as gBGxBuffer + 0x20 (in u16 units).
     * The original code does "subs r1, #0x40" to get to row 0. */
    u16* dst = bgBuffer - 0x20;

    if (ydiff < 8) {
        /* First row: copy without advancing src */
        memcpy(dst, src, 64); /* 32 u16 = 64 bytes */
        dst += 32;
        /* 22 more rows: first reuses same src, then advances */
        for (int i = 0; i < 22; i++) {
            memcpy(dst, src, 64);
            src += 0x100; /* next 8×8 map row = 128 u16 = 256 bytes */
            dst += 32;
        }
    } else {
        /* Start one map row earlier */
        src -= 0x100;
        /* 23 consecutive rows */
        for (int i = 0; i < 23; i++) {
            memcpy(dst, src, 64);
            src += 0x100;
            dst += 32;
        }
    }
}

/* Declared in screenTileMap.c (already compiled as C on PC) */
extern void sub_0807D280(u16* mapSpecial, u16* bgBuffer);
extern void sub_0807D46C(u16* mapSpecial, u16* bgBuffer);
extern void sub_0807D6D8(u16* mapSpecial, u16* bgBuffer);

/*
 * Collision direction masks from gUnk_0800275C (now in data_stubs_autogen.c)
 * Local alias for convenient u16 access.
 */
extern const u8 gUnk_0800275C[64];
#define gCollisionDirectionMasks ((const u16*)gUnk_0800275C)

/* Collision parameter tables (extended tile types, stubbed for now) */
extern const u8 gUnk_080082DC[];
extern const u8 gUnk_0800833C[];
extern const u8 gUnk_0800839C[];
extern const u8 gUnk_080083FC[];
extern const u8 gUnk_0800845C[];
extern const u8 gUnk_080084BC[];
extern const u8 gUnk_0800851C[];

/*
 * TileCollisionLookup — core tile collision lookup (port of sub_080086D8).
 *
 * Takes pixel coordinates (room-relative), looks up the collision data
 * for the tile at that position, and returns 0 (passable) or 1 (blocked).
 *
 * For tile collision types 0x00–0x0F: 2×2 sub-tile collision pattern.
 *   Bit 0 = bottom-right, bit 1 = bottom-left, bit 2 = top-right, bit 3 = top-left.
 *   The sub-tile quadrant is selected by bits 3 of x and y.
 *
 * For tile collision type 0xFF: always blocked.
 * For tile collision types 0x10–0xFE: extended pixel-level collision
 *   (returns 0/passable for now since lookup tables are stubbed).
 */
static u32 TileCollisionLookup(u32 px, u32 py, Entity* entity) {
    u32 maskedX = px & 0x3F0u;
    u32 maskedY = py & 0x3F0u;
    u32 tileX = maskedX >> 4;
    u32 tileY = maskedY >> 4;
    u32 index = tileX + tileY * 64;

    /* Get collision data for the entity's layer */
    u8* collisionData;
    u8 layer = entity->collisionLayer;
    if (layer == 2)
        collisionData = gMapTop.collisionData;
    else
        collisionData = gMapBottom.collisionData;

    u8 tileType = collisionData[index];

    /* Swimming check: if swimming and not in deep water, force block non-water tiles */
    if (gPlayerState.swim_state != 0 && gPlayerState.floor_type != 0x18) {
        if (tileType >= 0x10)
            goto extended;
        tileType = 0x0F; /* all quadrants blocked */
    }

    if (tileType < 0x10) {
        /* Simple 2×2 sub-tile collision.
         * Top half (y&8 == 0) → use bits 2-3 (>>2); bottom half → bits 0-1.
         * Left half (x&8 == 0) → >>1 additionally. */
        u32 result = tileType;
        if (!(py & 8))
            result >>= 2;
        if (!(px & 8))
            result >>= 1;
        return result & 1;
    }

extended:
    if (tileType == 0xFF)
        return 1; /* always blocked */

    /* Extended collision types (0x10–0xFE) — requires param tables from ROM.
     * Treat as passable for now. */
    return 0;
}

/*
 * sub_080085CC — fill entity->collisions by probing 8 points around entity hitbox.
 * (from Thumb asm at 0x080085CC)
 *
 * Probes are placed on the 4 edges of the collision box:
 *   Right edge:  (baseX + halfW, baseY ± vStep)
 *   Left edge:   (baseX - halfW, baseY ± vStep)
 *   Bottom edge: (baseX ± hStep, baseY + halfH)
 *   Top edge:    (baseX ± hStep, baseY - halfH)
 *
 * Hitbox layout (bytes 2–5 of Hitbox struct = unk2[0..3]):
 *   unk2[0] = halfW  (half-width for collision)
 *   unk2[1] = vStep  (vertical probe step)
 *   unk2[2] = hStep  (horizontal probe step)
 *   unk2[3] = halfH  (half-height for collision)
 *
 * Result is a 16-bit mask stored in entity->collisions:
 *   Bit 14: right edge, south probe
 *   Bit 13: right edge, north probe
 *   Bit 10: left edge, south probe
 *   Bit  9: left edge, north probe
 *   Bit  6: bottom edge, east probe
 *   Bit  5: bottom edge, west probe
 *   Bit  2: top edge, east probe
 *   Bit  1: top edge, west probe
 */
void sub_080085CC(Entity* ent) {
    /* Null hitbox guard */
    Hitbox* hb = ent->hitbox;
    if (hb == NULL) {
        ent->collisions = 0;
        return;
    }

    /* Get entity position relative to room origin */
    s32 relX = (s32)(u16)ent->x.HALF.HI - (s32)gRoomControls.origin_x;
    s32 relY = (s32)(u16)ent->y.HALF.HI - (s32)gRoomControls.origin_y;

    /* Hitbox offsets */
    s32 baseX = relX + hb->offset_x;
    s32 baseY = relY + hb->offset_y;

    u8 halfW = hb->unk2[0];
    u8 vStep = hb->unk2[1];
    u8 hStep = hb->unk2[2];
    u8 halfH = hb->unk2[3];

    s32 rightX = baseX + halfW;
    s32 leftX = baseX - halfW;
    s32 bottomY = baseY + halfH;
    s32 topY = baseY - halfH;

    u16 col = 0;

    /* Right edge: south & north */
    col |= TileCollisionLookup((u32)rightX, (u32)(baseY + vStep), ent) << 14;
    col |= TileCollisionLookup((u32)rightX, (u32)(baseY - vStep), ent) << 13;

    /* Left edge: south & north */
    col |= TileCollisionLookup((u32)leftX, (u32)(baseY + vStep), ent) << 10;
    col |= TileCollisionLookup((u32)leftX, (u32)(baseY - vStep), ent) << 9;

    /* Bottom edge: east & west */
    col |= TileCollisionLookup((u32)(baseX + hStep), (u32)bottomY, ent) << 6;
    col |= TileCollisionLookup((u32)(baseX - hStep), (u32)bottomY, ent) << 5;

    /* Top edge: east & west */
    col |= TileCollisionLookup((u32)(baseX + hStep), (u32)topY, ent) << 2;
    col |= TileCollisionLookup((u32)(baseX - hStep), (u32)topY, ent) << 1;

    ent->collisions = col;
}

/*
 * CalcCollisionDirectionOLD — adjust movement direction based on collision data
 * When an entity is moving in a cardinal direction and hitting a wall,
 * this returns a perpendicular direction to slide along the wall.
 * (from Thumb asm at 0x08002864)
 *
 * r0 = direction (0–31), r1 = collisions (entity->collisions)
 * Returns adjusted direction or original if no slide.
 */
static u32 CalcCollisionDirectionOLD(u32 direction, u32 collisions) {
    u32 quadrant = direction >> 3; /* 0=N, 1=E, 2=S, 3=W */

    switch (quadrant) {
        case 0: /* North */
            if (!(collisions & 0x000E))
                return direction;
            if (!(collisions & 0xE004))
                return 0x08; /* East */
            if (!(collisions & 0x0E02))
                return 0x18; /* West */
            return direction;

        case 1: /* East */
            if (!(collisions & 0xE000))
                return direction;
            if (!(collisions & 0x200E))
                return 0x00; /* North */
            if (!(collisions & 0x40E0))
                return 0x10; /* South */
            return direction;

        case 2: /* South */
            if (!(collisions & 0x00E0))
                return direction;
            if (!(collisions & 0xE040))
                return 0x08; /* East */
            if (!(collisions & 0x0E20))
                return 0x18; /* West */
            return direction;

        case 3: /* West */
            if (!(collisions & 0x0E00))
                return direction;
            if (!(collisions & 0x020E))
                return 0x00; /* North */
            if (!(collisions & 0x04E0))
                return 0x10; /* South */
            return direction;

        default:
            return direction;
    }
}

/*
 * LinearMoveDirectionOLD — move entity by speed in given direction, checking collisions.
 * (from Thumb asm at 0x080027EA)
 *
 * Updates entity->x.WORD and entity->y.WORD using sine table lookup.
 * Returns bitmask: bit 0 = X moved, bit 1 = Y moved.
 */
u32 LinearMoveDirectionOLD(Entity* ent, u32 speed, u32 direction) {
    u32 moved = 0;

    if (direction & 0x80)
        return 0; /* DIR_NOT_MOVING */

    u16 collisions = ent->collisions;

    /* If direction is exactly cardinal (no sub-cardinal bits), try wall sliding */
    if ((direction & 7) == 0) {
        u32 adjusted = CalcCollisionDirectionOLD(direction, collisions);
        if (adjusted != direction) {
            direction = adjusted;
            speed = 0x100; /* slow down when sliding */
        }
    }

    /* Look up collision mask for this direction */
    u16 colMask = gCollisionDirectionMasks[direction & 0x1F];
    u16 masked = collisions & colMask;

    /* X movement */
    if (!(masked & 0xEE00)) {
        s16 sinVal = gSineTable[direction * 8];
        if (sinVal != 0) {
            moved |= 1;
            s32 dx = FixedMul(sinVal, (s16)speed) << 8;
            ent->x.WORD += dx;
        }
    }

    /* Y movement */
    if (!(masked & 0x00EE)) {
        s16 cosVal = gSineTable[direction * 8 + 64];
        if (cosVal != 0) {
            moved |= 2;
            s32 dy = FixedMul(cosVal, (s16)speed) << 8;
            ent->y.WORD -= dy;
        }
    }

    return moved;
}

/*
 * sub_0800857C — player movement wrapper.
 * Calls sub_080085CC (collision tile update) then LinearMoveDirectionOLD.
 * (from Thumb asm at 0x0800857C)
 */
void sub_0800857C(Entity* ent) {
    sub_080085CC(ent);
    LinearMoveDirectionOLD(ent, ent->speed, ent->direction);
}

/*
 * sub_080085B0 — collision tile update wrapper.
 * (from Thumb asm at 0x080085B0)
 * Calls sub_080085CC — tile collision detection for the player.
 */
void sub_080085B0(Entity* ent) {
    sub_080085CC(ent);
}

/*
 * sub_08008AA0 — set player velocity direction from sine table.
 * (from Thumb asm at 0x08008AA0)
 *   Reads gPlayerState.direction, looks up sin/cos, stores in vel_x/vel_y.
 */
void sub_08008AA0(Entity* ent) {
    (void)ent;
    if (gPlayerState.floor_type == 1) /* SURFACE_AUTO */
        return;
    u8 dir = gPlayerState.direction;
    if (dir == 0xFF)
        return;
    gPlayerState.vel_x = gSineTable[dir * 8];
    gPlayerState.vel_y = -gSineTable[dir * 8 + 64];
}

/*
 * sub_08008AC6 — check if player should respawn (fallen off edge, etc.)
 * (from Thumb asm at 0x08008AC6)
 * Simplified stub: just check swim state and collision for respawn.
 */
void sub_08008AC6(Entity* ent) {
    /* Simplified: skip respawn check since it depends on GetNonCollidedSide
     * and RespawnPlayer which would need more infrastructure */
    (void)ent;
}

/*
 * GetNextFunction — entity state machine dispatcher
 * Returns 0-5 based on entity combat/action state.
 * (from Thumb asm at 0x0800279C)
 */
u32 GetNextFunction(Entity* this) {
    u8 gustJarState = this->gustJarState;
    u8 contactFlags = this->contactFlags;

    if (!(gustJarState & 4) && (contactFlags >> 7))
        return 1; /* contact initiated */

    if (this->knockbackDuration != 0)
        return 2; /* knockback active */

    if (this->health == 0) {
        if (this->action == 0 && this->subAction == 0)
            return 0; /* dead but not initialized */
        if (gustJarState & 8)
            return 5; /* gust jar captured and dead */
        if (this->confusedTime != 0)
            return 4; /* confused */
        return 3;     /* dying */
    }

    if (this->action == 0 && this->subAction == 0)
        return 0; /* not initialized */

    return 0; /* normal update */
}

/*
 * Random — simple 32-bit PRNG (from ARM asm at 0x08000E50)
 *   state = ROR(state * 3, 13);  return state >> 1;
 */
u32 Random(void) {
    gRand = gRand * 3;
    gRand = (gRand >> 13) | (gRand << 19); /* rotate right by 13 */
    return gRand >> 1;
}

/*
 * CheckBits — test whether `count` consecutive bits starting at `bitIndex`
 *             are all set in the byte array `base`.  (from ARM asm at 0x080B20EC)
 *   Returns 1 if ALL bits set, 0 if any bit is clear.
 */
u32 CheckBits(void* base, u32 bitIndex, u32 count) {
    const u8* ptr = (const u8*)base + (bitIndex / 8);
    u32 bit = bitIndex % 8;
    for (u32 i = 0; i < count; i++) {
        if (!(ptr[0] & (1u << bit)))
            return 0;
        bit++;
        if (bit >= 8) {
            bit = 0;
            ptr++;
        }
    }
    return 1;
}

/*
 * Mod — modulo (SWI 0x06 wrapper): returns num % denom
 */
s32 Mod(s32 num, s32 denom) {
    if (denom == 0)
        return 0;
    return num % denom;
}

/*
 * SumDropProbabilities — vector add 3 arrays of 16 s16 values.
 *   out[i] = a[i] + b[i] + c[i]   for i = 0..15
 */
void SumDropProbabilities(s16* out, const s16* a, const s16* b, const s16* c) {
    for (int i = 0; i < 16; i++) {
        out[i] = a[i] + b[i] + c[i];
    }
}

/*
 * SumDropProbabilities2 — vector add, clamp each to >= 0, return scalar sum.
 *   out[i] = max(0, a[i] + b[i] + c[i])   for i = 0..15
 *   returns sum of all out[i]
 */
u32 SumDropProbabilities2(s16* out, const s16* a, const s16* b, const s16* c) {
    u32 sum = 0;
    for (int i = 0; i < 16; i++) {
        s16 val = a[i] + b[i] + c[i];
        if (val < 0)
            val = 0;
        out[i] = val;
        sum += val;
    }
    return sum;
}

/*
 * UpdateScrollVram — copies map tile data from gMapDataBottomSpecial / gMapDataTopSpecial
 * into gBG1Buffer / gBG2Buffer depending on gUpdateVisibleTiles.
 *
 * Replaces the ARM veneer at 0x08000108 and the IWRAM sub_080B197C.
 */
void UpdateScrollVram(void) {
    typedef void (*ScrollVramFunc)(u16*, u16*);
    static const ScrollVramFunc funcs[] = {
        NULL,               /* 0: unused (returns immediately) */
        ram_sub_080B197C_c, /* 1: initial full-screen fill */
        sub_0807D280,       /* 2: scroll update mode */
        sub_0807D46C,       /* 3: scroll update mode */
        sub_0807D6D8,       /* 4: scroll update mode */
    };

    u8 mode = gUpdateVisibleTiles;
    if (mode == 0 || mode > 4)
        return;

    ScrollVramFunc func = funcs[mode];

    /* Bottom layer → BG1 */
    if (gMapBottom.bgSettings != NULL) {
        func(gMapDataBottomSpecial, &gBG1Buffer[0x20]);
    }

    /* Top layer → BG2 */
    if (gMapTop.bgSettings != NULL) {
        func(gMapDataTopSpecial, &gBG2Buffer[0x20]);
    }
}

/*
 * UpdateSpriteForCollisionLayer — sets OBJ priority bits based on entity's collision layer.
 * (from Thumb asm at 0x08016A04)
 *
 * Table embedded in ROM:
 *   Layer 0: spriteRendering b3=0x80 (priority 2), spriteOrientation flipY=0x80 (priority 2)
 *   Layer 1: same as layer 0
 *   Layer 2: spriteRendering b3=0x40 (priority 1), spriteOrientation flipY=0x40 (priority 1)
 *   Layer 3: same as layer 2
 *
 * This ensures entities on the bottom layer render behind the top BG layer (tree canopy, roofs)
 * and entities on the top layer render in front of the top BG layer.
 */
void UpdateSpriteForCollisionLayer(Entity* entity) {
    static const u8 sCollisionLayerPriorityTable[8] = {
        0x80, 0x80, /* layer 0: priority 2 */
        0x80, 0x80, /* layer 1: priority 2 */
        0x40, 0x40, /* layer 2: priority 1 */
        0x40, 0x40, /* layer 3: priority 1 */
    };

    u8 layer = entity->collisionLayer;
    if (layer > 3)
        layer = 0;
    const u8* entry = &sCollisionLayerPriorityTable[layer * 2];

    /* Set bits 6-7 of spriteRendering (offset 0x19) */
    u8* renderByte = (u8*)&entity->spriteRendering;
    *renderByte = (*renderByte & ~0xC0) | entry[0];

    /* Set bits 6-7 of spriteOrientation (offset 0x1b) */
    u8* orientByte = (u8*)&entity->spriteOrientation;
    *orientByte = (*orientByte & ~0xC0) | entry[1];
}

// Area / room data
const AreaHeader gAreaMetadata[256];
/**
 * GravityUpdate — port of ARM thumb routine at 0x08003FC4.
 * Applies gravity to an entity's z-axis each frame.
 */
u32 GravityUpdate(Entity* entity, u32 gravity) {
    s32 z = (s32)entity->z.WORD - (s32)entity->zVelocity;
    if (z < 0) {
        entity->z.WORD = (u32)z;
        entity->zVelocity = (s32)entity->zVelocity - (s32)gravity;
        return (u32)z;
    } else {
        entity->z.WORD = 0;
        entity->zVelocity = 0;
        return 0;
    }
}

/**
 * BounceUpdate — port of Thumb routine at 0x080044EC.
 * Like GravityUpdate but with bouncing: when entity hits ground,
 * calculates a reduced bounce velocity.
 *
 * Returns: 2 = airborne, 1 = just bounced, 0 = done bouncing
 */
u32 BounceUpdate(Entity* entity, u32 acceleration) {
    s32 z = (s32)entity->z.WORD - (s32)entity->zVelocity;
    if (z < 0) {
        /* Still airborne */
        entity->z.WORD = (u32)z;
        entity->zVelocity = (s32)entity->zVelocity - (s32)acceleration;
        return 2;
    }
    /* Hit ground — calculate bounce */
    entity->z.WORD = 1; /* z=1 signals "just landed" (player can't do certain actions at z!=0) */
    s32 vel = (s32)entity->zVelocity - (s32)acceleration;
    vel = -vel;
    vel >>= 1;
    u32 uvel = (u32)vel;
    uvel = uvel + (uvel >> 2); /* vel * 1.25 — damped bounce */
    u32 result;
    if ((uvel >> 12) >= 0xC) {
        result = 1; /* Still has enough energy to bounce */
    } else {
        result = 0; /* Done bouncing */
        uvel = 0;
    }
    entity->zVelocity = uvel;
    return result;
}

/**
 * GetFacingDirection — port of Thumb routine at 0x080045C4.
 * Calculates direction (0-31 in 5-bit system) from entity A to entity B.
 * Falls through to CalculateDirectionTo in ASM.
 */
u32 GetFacingDirection(Entity* from, Entity* to) {
    return CalculateDirectionTo((s16)from->x.HALF.HI, (s16)from->y.HALF.HI, (s16)to->x.HALF.HI, (s16)to->y.HALF.HI);
}

/**
 * sub_080045B4 — port of Thumb routine at 0x080045B4.
 * Calculates direction from entity position to absolute (targetX, targetY).
 * In ASM: shuffles args then tail-calls ram_CalcCollisionDirection.
 */
u32 sub_080045B4(Entity* entity, u32 targetX, u32 targetY) {
    return CalculateDirectionTo((s16)entity->x.HALF.HI, (s16)entity->y.HALF.HI, (s16)targetX, (s16)targetY);
}

/**
 * EntityInRectRadius — port of Thumb routine at 0x080041A0.
 * Checks if two entities are within rectangular proximity AND share collision layers.
 *
 * Returns 1 if both axis-distance checks pass and entities share layer bits 0-1.
 * A radius of 0 on an axis skips that axis check (always passes).
 */
u32 EntityInRectRadius(Entity* a, Entity* b, u32 xRadius, u32 yRadius) {
    /* Collision layer check: both must share bits 0-1 */
    u8 sharedLayers = a->collisionLayer & b->collisionLayer;
    if ((sharedLayers & 3) == 0)
        return 0;

    /* X axis check */
    if (xRadius != 0) {
        s32 deltaX = (s16)a->x.HALF.HI - (s16)b->x.HALF.HI;
        u32 offsetX = (u32)(deltaX + (s32)xRadius);
        if (offsetX > xRadius * 2)
            return 0;
    }

    /* Y axis check */
    if (yRadius != 0) {
        s32 deltaY = (s16)a->y.HALF.HI - (s16)b->y.HALF.HI;
        u32 offsetY = (u32)(deltaY + (s32)yRadius);
        if (offsetY > yRadius * 2)
            return 0;
    }

    return 1;
}

/**
 * CheckPlayerInRegion — port of Thumb routine at 0x0800293E.
 * Checks whether the player entity is within a room-relative rectangle.
 *
 * The rectangle is centered at (x, y) with half-extents (halfWidth, halfHeight),
 * all relative to the room origin (gRoomControls.origin_x/y).
 */
u32 CheckPlayerInRegion(u32 x, u32 y, u32 halfWidth, u32 halfHeight) {
    s32 playerRelX = (s32)gPlayerEntity.base.x.HALF.HI - (s32)gRoomControls.origin_x;
    s32 playerRelY = (s32)gPlayerEntity.base.y.HALF.HI - (s32)gRoomControls.origin_y;

    /* Unsigned range check: (x - (playerRelX - halfWidth)) must be < 2*halfWidth */
    u32 offsetX = (u32)((s32)x - (playerRelX - (s32)halfWidth));
    if (offsetX >= halfWidth * 2)
        return 0;

    u32 offsetY = (u32)((s32)y - (playerRelY - (s32)halfHeight));
    if (offsetY >= halfHeight * 2)
        return 0;

    return 1;
}

/* ================================================================
 * Tile lookup functions — port of ARM routines in intr.s
 *
 * On GBA these use indirection tables (gActTilePtrs, gMapDataPtrs,
 * gCollisionDataPtrs) that point into gMapBottom / gMapTop fields.
 * On PC we can access the struct fields directly.
 *
 * Layer mapping:
 *   0, 1, 3 → gMapBottom
 *   2       → gMapTop
 *
 * Coordinate conversion (ARM shifts):
 *   lsl #22, lsr #26  ≡  (x & 0x3FF) >> 4  →  tile index 0..63
 * ================================================================ */

static inline MapLayer* GetMapLayerForLayer(u32 layer) {
    return (layer == 2) ? &gMapTop : &gMapBottom;
}

/* World pixel → room-relative tile position */
static inline u32 WorldToTilePos(u32 worldX, u32 worldY) {
    u32 roomX = worldX - gRoomControls.origin_x;
    u32 roomY = worldY - gRoomControls.origin_y;
    u32 tileX = ((roomX << 22) >> 26); /* (roomX & 0x3FF) >> 4 */
    u32 tileY = ((roomY << 22) >> 26);
    return tileX + (tileY << 6); /* tileX + tileY * 64 */
}

/* Room pixel → tile position */
static inline u32 RoomToTilePos(u32 roomX, u32 roomY) {
    u32 tileX = ((roomX << 22) >> 26);
    u32 tileY = ((roomY << 22) >> 26);
    return tileX + (tileY << 6);
}

/* ---------- ActTile family ---------- */

/**
 * GetActTileAtTilePos — get act tile at a raw tile position.
 * (port of arm_GetActTileAtTilePos at 0x080B1AE0)
 */
u32 GetActTileAtTilePos(u16 tilePos, u8 layer) {
    MapLayer* ml = GetMapLayerForLayer(layer);
    return ml->actTiles[tilePos];
}

/**
 * GetActTileAtRoomTile — get act tile at room tile coordinates.
 * (port of arm_GetActTileAtRoomTile)
 */
u32 GetActTileAtRoomTile(u32 roomTileX, u32 roomTileY, u32 layer) {
    u32 tilePos = roomTileX + (roomTileY << 6);
    MapLayer* ml = GetMapLayerForLayer(layer);
    return ml->actTiles[tilePos];
}

/**
 * GetActTileAtRoomCoords — get act tile at room pixel coordinates.
 * (port of arm_GetActTileAtRoomCoords)
 */
u32 GetActTileAtRoomCoords(u32 roomX, u32 roomY, u32 layer) {
    u32 tilePos = RoomToTilePos(roomX, roomY);
    MapLayer* ml = GetMapLayerForLayer(layer);
    return ml->actTiles[tilePos];
}

/**
 * GetActTileAtWorldCoords — get act tile at world pixel coordinates.
 * (port of arm_GetActTileAtWorldCoords)
 */
u32 GetActTileAtWorldCoords(u32 worldX, u32 worldY, u32 layer) {
    u32 tilePos = WorldToTilePos(worldX, worldY);
    MapLayer* ml = GetMapLayerForLayer(layer);
    return ml->actTiles[tilePos];
}

/**
 * GetActTileAtEntity — get act tile under an entity.
 * (port of arm_GetActTileAtEntity)
 */
u32 GetActTileAtEntity(Entity* entity) {
    u32 tilePos = WorldToTilePos(entity->x.HALF.HI, entity->y.HALF.HI);
    MapLayer* ml = GetMapLayerForLayer(entity->collisionLayer);
    return ml->actTiles[tilePos];
}

/**
 * GetActTileRelativeToEntity — get act tile at entity position + offset.
 * (port of arm_GetActTileRelativeToEntity)
 */
u32 GetActTileRelativeToEntity(Entity* entity, s32 xOffset, s32 yOffset) {
    u32 worldX = (u16)entity->x.HALF.HI + xOffset;
    u32 worldY = (u16)entity->y.HALF.HI + yOffset;
    u32 tilePos = WorldToTilePos(worldX, worldY);
    MapLayer* ml = GetMapLayerForLayer(entity->collisionLayer);
    return ml->actTiles[tilePos];
}

/**
 * GetActTileForTileType — convert a tileType to its act tile value.
 * (port of arm_GetActTileForTileType at 0x080B1B54)
 *
 * tileType < 0x4000 → gMapTileTypeToActTile[tileType]
 * tileType >= 0x4000 → gMapSpecialTileToActTile[tileType - 0x4000]
 */
extern const u8 gMapTileTypeToActTile[];
extern const u16 gUnk_080B7A3E[]; /* gMapSpecialTileToActTile */
u32 GetActTileForTileType(u32 tileType) {
    if (tileType < 0x4000)
        return gMapTileTypeToActTile[tileType];
    else
        return ((const u8*)gUnk_080B7A3E)[tileType - 0x4000];
}

/* ---------- CollisionData family ---------- */

/**
 * GetCollisionDataAtTilePos — get collision byte at a raw tile position.
 * (port of arm_GetCollisionDataAtTilePos)
 */
u32 GetCollisionDataAtTilePos(u32 tilePos, u32 layer) {
    MapLayer* ml = GetMapLayerForLayer(layer);
    return ml->collisionData[tilePos];
}

u32 GetCollisionDataAtRoomCoords(u32 roomX, u32 roomY, u32 layer) {
    u32 tilePos = RoomToTilePos(roomX, roomY);
    MapLayer* ml = GetMapLayerForLayer(layer);
    return ml->collisionData[tilePos];
}

u32 GetCollisionDataAtWorldCoords(u32 worldX, u32 worldY, u32 layer) {
    u32 tilePos = WorldToTilePos(worldX, worldY);
    MapLayer* ml = GetMapLayerForLayer(layer);
    return ml->collisionData[tilePos];
}

u32 GetCollisionDataAtEntity(Entity* entity) {
    u32 tilePos = WorldToTilePos(entity->x.HALF.HI, entity->y.HALF.HI);
    MapLayer* ml = GetMapLayerForLayer(entity->collisionLayer);
    return ml->collisionData[tilePos];
}

u32 GetCollisionDataRelativeTo(Entity* entity, s32 xOffset, s32 yOffset) {
    u32 worldX = (u16)entity->x.HALF.HI + xOffset;
    u32 worldY = (u16)entity->y.HALF.HI + yOffset;
    u32 tilePos = WorldToTilePos(worldX, worldY);
    MapLayer* ml = GetMapLayerForLayer(entity->collisionLayer);
    return ml->collisionData[tilePos];
}

/* ---------- TileType family ---------- */

/**
 * GetTileTypeAtTilePos — get tile type at a raw tile position.
 * (port of arm_GetTileTypeAtTilePos at 0x080B1A60)
 *
 * Reads mapData[tilePos] → tileIndex.
 * If tileIndex >= 0x4000 → return tileIndex (special tile reference).
 * Otherwise → return tileTypes[tileIndex].
 */
u32 GetTileTypeAtTilePos(u32 tilePos, u32 layer) {
    MapLayer* ml = GetMapLayerForLayer(layer);
    u16 tileIndex = ml->mapData[tilePos];
    if (tileIndex >= 0x4000)
        return tileIndex;
    return ml->tileTypes[tileIndex];
}

u32 GetTileTypeAtRoomCoords(u32 roomX, u32 roomY, u32 layer) {
    u32 tilePos = RoomToTilePos(roomX, roomY);
    return GetTileTypeAtTilePos(tilePos, layer);
}

u32 GetTileTypeAtWorldCoords(s32 worldX, s32 worldY, u32 layer) {
    u32 tilePos = WorldToTilePos((u32)worldX, (u32)worldY);
    return GetTileTypeAtTilePos(tilePos, layer);
}

u32 GetTileTypeAtEntity(Entity* entity) {
    u32 tilePos = WorldToTilePos(entity->x.HALF.HI, entity->y.HALF.HI);
    return GetTileTypeAtTilePos(tilePos, entity->collisionLayer);
}

u32 GetTileTypeRelativeToEntity(Entity* entity, s32 xOffset, s32 yOffset) {
    u32 worldX = (u16)entity->x.HALF.HI + xOffset;
    u32 worldY = (u16)entity->y.HALF.HI + yOffset;
    u32 tilePos = WorldToTilePos(worldX, worldY);
    return GetTileTypeAtTilePos(tilePos, entity->collisionLayer);
}

/* ---------- sub_080B1B84 / sub_080B1BA4 — tile data lookup ---------- */

/**
 * sub_080B1B84 — look up tile property data (u16) from gUnk_08000360 table.
 * (port of arm_sub_080B1B84 at 0x080B1B84)
 *
 * Calls GetTileTypeAtTilePos, then indexes into gUnk_08000360 or gUnk_080B7A3E
 * (based on whether tileType < 0x4000 or not) as a u16 array.
 */
u32 sub_080B1B84(u32 tilePos, u32 layer) {
    u32 tileType = GetTileTypeAtTilePos(tilePos, layer);
    const u16* table;
    if (tileType < 0x4000) {
        /* gUnk_08000360 is at ROM offset 0x360 */
        table = (const u16*)&gRomData[0x360];
    } else {
        table = gUnk_080B7A3E;
    }
    return table[tileType & 0x3FFF];
}

/**
 * sub_080B1BA4 — like sub_080B1B84 but AND result with a mask (r2).
 * (port of arm_sub_080B1BA4 at 0x080B1BA4)
 */
u32 sub_080B1BA4(u32 tilePos, u32 layer, u32 mask) {
    u32 tileType = GetTileTypeAtTilePos(tilePos, layer);
    const u16* table;
    if (tileType < 0x4000) {
        table = (const u16*)&gRomData[0x360];
    } else {
        table = gUnk_080B7A3E;
    }
    return table[tileType & 0x3FFF] & mask;
}

/* ---------- SetCollisionData, SetActTileAtTilePos, GetTileIndex, SetTile, CloneTile ---------- */

extern const u8 gMapSpecialTileToActTile[];
extern const u8 gMapSpecialTileToCollisionData[];
extern const u8 gMapTileTypeToCollisionData[];
extern void UnregisterInteractTile(u32, u32);
extern void RegisterInteractTile(u32, u32, u32);

/**
 * SetCollisionData — set collision data at a tile position.
 * (port of Thumb veneer at 0x08000148)
 *
 * gCollisionDataPtrs layout (by layer):
 *   0 → gMapBottom.collisionData
 *   1 → gMapBottom.collisionData
 *   2 → gMapTop.collisionData
 *   3 → gMapBottom.collisionData
 */
void SetCollisionData(u32 collisionData, u32 tilePos, u32 layer) {
    MapLayer* ml = GetMapLayerForLayer(layer);
    ml->collisionData[tilePos] = collisionData;
}

/**
 * SetActTileAtTilePos — set act tile at a tile position.
 * (port of Thumb veneer at 0x080001D0)
 *
 * gActTilePtrs layout (by layer):
 *   0 → gMapBottom.actTiles
 *   1 → gMapBottom.actTiles
 *   2 → gMapTop.actTiles
 *   3 → gMapBottom.actTiles
 */
void SetActTileAtTilePos(u32 actTile, u32 tilePos, u32 layer) {
    MapLayer* ml = GetMapLayerForLayer(layer);
    ml->actTiles[tilePos] = actTile;
}

/**
 * GetTileIndex — get the tile index (mapData value) at a tile position.
 * (port of Thumb veneer at 0x080001DA)
 *
 * Returns mapData[tilePos] for the specified layer.
 */
u32 GetTileIndex(u32 tilePos, u32 layer) {
    MapLayer* ml = GetMapLayerForLayer(layer);
    return ml->mapData[tilePos];
}

/**
 * SetTile — set a tile at a position and update collision/actTile data.
 * (port of Thumb veneer at 0x0800015C)
 *
 * If tileIndex >= 0x4000 (special tile):
 *   - Uses gMapSpecialTileToActTile / gMapSpecialTileToCollisionData
 *   - Calls UnregisterInteractTile + RegisterInteractTile for tile entity management
 * Else (normal tile):
 *   - Looks up tileType from tileTypes[tileIndex]
 *   - Uses gMapTileTypeToActTile / gMapTileTypeToCollisionData
 *   - Calls UnregisterInteractTile
 */
void SetTile(u32 tileIndex, u32 tilePos, u32 layer) {
    MapLayer* ml = GetMapLayerForLayer(layer);
    u16 oldTile = ml->mapData[tilePos];
    ml->mapData[tilePos] = (u16)tileIndex;

    if (tileIndex >= 0x4000) {
        /* Special tile */
        u32 specialIdx = tileIndex - 0x4000;
        SetActTileAtTilePos(gMapSpecialTileToActTile[specialIdx], tilePos, layer);
        SetCollisionData(gMapSpecialTileToCollisionData[specialIdx], tilePos, layer);
        UnregisterInteractTile(tilePos, layer);
        RegisterInteractTile(oldTile, tilePos, layer);
    } else {
        /* Normal tile — look up tile type from tileTypes table */
        u16 tileType = ml->tileTypes[tileIndex];
        SetActTileAtTilePos(gMapTileTypeToActTile[tileType], tilePos, layer);
        SetCollisionData(gMapTileTypeToCollisionData[tileType], tilePos, layer);
        UnregisterInteractTile(tilePos, layer);
    }
}

/**
 * CloneTile — look up the tile index from a tile type and call SetTile.
 * (port of Thumb veneer at 0x08000152)
 *
 * Looks up tileIndices[tileType] for the given layer to get the actual tile index,
 * then falls through to SetTile(tileIndex, tilePos, layer).
 */
void CloneTile(u32 tileType, u32 tilePos, u32 layer) {
    MapLayer* ml = GetMapLayerForLayer(layer);
    u16 tileIndex = ml->tileIndices[tileType];
    SetTile(tileIndex, tilePos, layer);
}

/* ---------- ResolveCollisionLayer, CheckOnLayerTransition, UpdateCollisionLayer ---------- */

/**
 * Transition tile table entry for layer transitions.
 * (from ARM asm at 0x08016A90 / gTransitionTiles)
 */
typedef struct {
    u16 actTile;
    u8 fromLayer;
    u8 toLayer;
} TransitionTileEntry;

static const TransitionTileEntry sTransitionTiles[] = {
    { 0x0A, 2, 1 }, { 0x09, 2, 1 }, { 0x0C, 1, 2 }, { 0x0B, 1, 2 },
    { 0x52, 3, 3 }, { 0x27, 3, 3 }, { 0x26, 3, 3 }, { 0x0000, 0, 0 }, /* terminator */
};

/**
 * Table of act tiles to check for ResolveCollisionLayer.
 * (from ARM asm above gTransitionTiles)
 */
static const TransitionTileEntry sResolveCollisionLayerTiles[] = {
    { 0x2A, 3, 3 }, { 0x2D, 3, 3 }, { 0x2B, 3, 3 }, { 0x2C, 3, 3 },   { 0x4C, 3, 3 },
    { 0x4E, 3, 3 }, { 0x4D, 3, 3 }, { 0x4F, 3, 3 }, { 0x0000, 0, 0 }, /* terminator */
};

/**
 * ResolveCollisionLayer — determine which collision layer an entity should be on
 * based on the tile under it.
 * (port of ARM asm at 0x08016A30)
 */
u32 ResolveCollisionLayer(Entity* entity) {
    if (entity->collisionLayer == 0) {
        u32 tileType = GetTileTypeAtWorldCoords(entity->x.HALF.HI, entity->y.HALF.HI, 2);
        u8 newLayer = 1;
        if (tileType != 0) {
            u32 actTile = GetActTileForTileType(tileType);
            newLayer = 2;
            /* Check against the resolve table */
            const TransitionTileEntry* p = sResolveCollisionLayerTiles;
            while (p->actTile != 0) {
                if (actTile == p->actTile) {
                    newLayer = p->toLayer;
                    break;
                }
                p++;
            }
        }
        entity->collisionLayer = newLayer;
    }
    UpdateSpriteForCollisionLayer(entity);
    return 0;
}

/**
 * CheckOnLayerTransition — check if entity is standing on a layer-transition tile.
 * (port of ARM asm at 0x08016A68)
 *
 * Returns the act tile at the entity's position (preserved in r0 in ARM code).
 */
u32 CheckOnLayerTransition(Entity* entity) {
    u32 actTile = GetActTileAtEntity(entity);
    const TransitionTileEntry* p = sTransitionTiles;
    while (p->actTile != 0) {
        if (p->actTile == actTile) {
            if (entity->collisionLayer != p->fromLayer) {
                entity->collisionLayer = p->toLayer;
            }
            return actTile;
        }
        p++;
    }
    return actTile;
}

/**
 * UpdateCollisionLayer — check layer transition and update sprite priority.
 * (port of ARM asm at 0x08016AB4)
 */
void UpdateCollisionLayer(Entity* entity) {
    CheckOnLayerTransition(entity);
    UpdateSpriteForCollisionLayer(entity);
}

/**
 * GetTileHazardType — check if the entity is standing on a hazardous tile.
 * (port of ARM asm at 0x080043E8)
 *
 * Hazard list:
 *   0x0D → 1 (hole)
 *   0x10 → 2 (lava)
 *   0x11 → 2 (lava)
 *   0x5A → 3 (swamp)
 *   0x13 → 4 (water/drown)
 *
 * Returns 0 if entity has no action, is in air (z < 0), or not on a hazard tile.
 */
u32 GetTileHazardType(Entity* entity) {
    static const struct {
        u16 actTile;
        u16 hazardType;
    } sHazardList[] = {
        { 0x0D, 1 }, { 0x10, 2 }, { 0x11, 2 }, { 0x5A, 3 }, { 0x13, 4 }, { 0x00, 0 }, /* terminator */
    };

    if (entity->action == 0)
        return 0;

    UpdateCollisionLayer(entity);
    u32 actTile = GetActTileAtEntity(entity);

    /* Check z position — if entity is in the air, no hazard */
    if ((s16)entity->z.HALF.HI < 0)
        return 0;

    if (actTile == 0)
        return 0;

    for (int i = 0; sHazardList[i].actTile != 0; i++) {
        if (actTile == sHazardList[i].actTile)
            return sHazardList[i].hazardType;
    }
    return 0;
}

RoomHeader* gAreaRoomHeaders[256];
void* gAreaRoomMaps[256];
void* gAreaTable[256];
void* gAreaTileSets[256];
void* gAreaTiles[256];

// Function pointer tables
/* gSubtasks -- defined in port_stubs.c as proper function pointer table */
// ButtonUIElement_Actions — defined in ui.c with proper function pointers
// EzloNagUIElement_Actions — defined in ui.c with proper function pointers
void* HoleManager_Actions[16];
// gUIElementDefinitions — defined in ui.c with proper UIElementDefinition type
void* Subtask_FastTravel_Functions[16];
void* Subtask_MapHint_Functions[16];

// Exit lists / transitions (populated from ROM at load time)
const Transition* const* gExitLists[256];
const Transition gExitList_RoyalValley_ForestMaze[4];

// Various game data
u32 gFixedTypeGfxData[528];
void* gCaveBorderMapData[16];
u8 gMessageChoices[32] __attribute__((aligned(4)));
u8 gOverworldLocations[256] __attribute__((aligned(4)));
void* gMoreSpritePtrs[64];
u8 gExtraFrameOffsets[256];
u8 gShakeOffsets[256];
u16 gDungeonNames[64];
u8 gFigurines[512] __attribute__((aligned(4)));
void* gLilypadRails[32];
u8 gMapActTileToSurfaceType[256] __attribute__((aligned(4)));
u8 gPalette_549[32];
void* gTranslations[16];
void* gWallMasterScreenTransitions[16];
void* gZeldaFollowerText[8];
void* gSpriteAnimations_322[64];
u32 gSpriteAnimations_GhostBrothers[64];
u8 gDiggingCaveEntranceTransition[32] __attribute__((aligned(4)));
u8 RupeeKeyDigits[16];

// Player macros
u8 gPlayerMacroBladeBrothers0[16], gPlayerMacroBladeBrothers1[16];
u8 gPlayerMacroBladeBrothers2[16], gPlayerMacroBladeBrothers3[16];
u8 gPlayerMacroBladeBrothers4[16], gPlayerMacroBladeBrothers5[16];
u8 gPlayerMacroBladeBrothers6[16], gPlayerMacroBladeBrothers7[16];

// Entity data (ROM data — all zero-init stubs)
u8 Entities_HouseInteriors1_Mayor_080D6210[64];
u8 Entities_MinishPaths_MayorsCabin_gUnk_080D6138[64];
u8 UpperInn_Din[64], UpperInn_Farore[64], UpperInn_Nayru[64];
u8 UpperInn_NoDin[64], UpperInn_NoFarore[64], UpperInn_NoNayru[64], UpperInn_Oracles[64];
u8 gUnk_additional_8_DeepwoodShrine_StairsToB1[64];
u8 gUnk_additional_8_HouseInteriors1_Library1F[64];
u8 gUnk_additional_8_HouseInteriors3_BorlovEntrance[64];
u8 gUnk_additional_8_HyruleCastle_3[64];
u8 gUnk_additional_8_MelarisMine_Main[64];
u8 gUnk_additional_8_PalaceOfWinds_GyorgTornado[64];
u8 gUnk_additional_9_HouseInteriors1_Library1F[64];
u8 gUnk_additional_9_HouseInteriors2_Percy[64];
u8 gUnk_additional_9_HouseInteriors3_BorlovEntrance[64];
u8 gUnk_additional_9_MelarisMine_Main[64];
u8 gUnk_additional_9_PalaceOfWinds_GyorgTornado[64];
u8 gUnk_additional_a_CaveOfFlamesBoss_Main[64];
u8 gUnk_additional_a_DeepwoodShrineBoss_Main[64];
u8 gUnk_additional_a_HouseInteriors2_Percy[64];
u8 gUnk_additional_a_HouseInteriors3_BorlovEntrance[64];
u8 gUnk_additional_a_TempleOfDroplets_BigOcto[64];
u8 gUnk_additional_c_HouseInteriors2_Romio[64];
u32 Enemies_LakeHylia_Main;
u32 Area_HyruleTown[16];

// Script data (ROM data — zero-init stubs so linker resolves them)
u16 script_08012C48;
u16 script_08015B14;
u16 script_BedAtSimons;
u16 script_BedInLinksRoom;
u8 script_BombMinish[32], script_BombMinishKinstone[32];
u16 script_BusinessScrubIntro[16];
u16 script_CutsceneMiscObjectSwordInChest;
u16 script_CutsceneMiscObjectTheLittleHat;
u16 script_EzloTalkOcarina[16];
u8 script_ForestMinish1[32], script_ForestMinish2[32], script_ForestMinish3[32];
u8 script_ForestMinish4[32], script_ForestMinish5[32], script_ForestMinish6[32];
u8 script_ForestMinish7[32], script_ForestMinish8[32], script_ForestMinish9[32];
u8 script_ForestMinish10[32], script_ForestMinish11[32], script_ForestMinish12[32];
u8 script_ForestMinish13[32], script_ForestMinish14[32], script_ForestMinish15[32];
u8 script_ForestMinish16[32], script_ForestMinish17[32], script_ForestMinish18[32];
u8 script_ForestMinish19[32], script_ForestMinish20[32], script_ForestMinish21[32];
u16 script_MazaalBossObjectMazaal[16];
u16 script_MazaalMacroDefeated[16];
u8 script_MinishVillageObjectLeftStoneOpening[4];
u8 script_MinishVillageObjectRightStoneOpening[4];
u16 script_PlayerAtDarkNut1[16], script_PlayerAtDarkNut2[16], script_PlayerAtDarkNut3[16];
u16 script_PlayerAtMadderpillar[16];
u16 script_PlayerGetElement[16];
u32 script_PlayerIntro;
void* script_PlayerSleepingInn[8];
u32 script_PlayerWakeAfterRest;
u32 script_PlayerWakingUpAtSimons;
u32 script_PlayerWakingUpInHyruleCastle;
u8 script_Rem[4];
u16 script_Stockwell;
u16 script_StockwellBuy[16];
u16 script_StockwellDogFood[16];
u8 script_TalonGotKey;
u16 script_WindTribespeople6;
u16 script_ZeldaMagic;

// Unk_08133368 — already defined in data_stubs_autogen.c

/* ================================================================
 * Enemy update system — ported from ARM Thumb assembly (enemy.s,
 * code_08001A7C.s, code_080043E8.s)
 * ================================================================ */

extern void DeleteThisEntity(void);
extern u32 EnemyInit(Entity*);
extern u32 EntityDisabled(Entity*);
extern void DrawEntity(Entity*);
extern void Knockback1(Entity*);
extern void Knockback2(Entity*);
extern void EnemyDetachFX(Entity*);
extern void UpdateAnimationVariableFrames(Entity*, u32);
extern void CreatePitFallFX(Entity*);
extern void CreateDrownFX(Entity*);
extern void CreateLavaDrownFX(Entity*);
extern void CreateSwampDrownFX(Entity*);
extern u32 Random(void);
extern void (*gEnemyFunctions[])(Entity*);

/*
 * sub_080028E0 — Decrement iframes towards zero.
 * Port of 0x080028E0 from code_08001A7C.s.
 */
void sub_080028E0(Entity* entity) {
    if (entity->iframes > 0)
        entity->iframes--;
    else if (entity->iframes < 0)
        entity->iframes++;
}

/*
 * GetRandomByWeight — pick a random index from a probability table.
 * Port of 0x080028F4 from code_08001A7C.s.
 */
u32 GetRandomByWeight(const u8* weights) {
    u32 r = Random() & 0xFF;
    u32 i = 0;
    for (;;) {
        r -= weights[i];
        i++;
        if ((s32)r < 0)
            break;
    }
    return i - 1;
}

/*
 * CheckRectOnScreen — test if a rectangle (centred at x,y with
 * half-extents halfW, halfH) overlaps the visible screen.
 * Port of 0x0800290E from code_08001A7C.s.
 */
u32 CheckRectOnScreen(s32 x, s32 y, u32 halfW, u32 halfH) {
    s32 sx = gRoomControls.scroll_x - gRoomControls.origin_x;
    u32 dx = (u32)(x - sx + halfW);
    if (dx >= halfW * 2 + 0xF0)
        return 0;
    s32 sy = gRoomControls.scroll_y - gRoomControls.origin_y;
    u32 dy = (u32)(y - sy + halfH);
    if (dy >= halfH * 2 + 0xA0)
        return 0;
    return 1;
}

/*
 * sub_080012DC — Check whether the enemy stands on a tile hazard.
 * Port of 0x080012DC from enemy.s.
 * Returns: 0 = no hazard (or filtered), 1 = hole, 2 = lava, 3 = swamp.
 * Drown (4) is filtered to 0.
 */
s32 sub_080012DC(Entity* entity) {
    /* If gustJarState bit 2 is set (grabbed/held), skip hazard check */
    if (entity->gustJarState & 4)
        return 0;

    u32 hazard = GetTileHazardType(entity);

    if (hazard == 4) /* drown → ignore */
        return 0;

    if (hazard == 0) {
        /* No hazard, but if gustJarState bit 0 is set, mark flag */
        if (entity->gustJarState & 1)
            entity->flags |= 0x80;
        return 0;
    }

    /* hazard 1 = hole → return directly */
    if (hazard == 1)
        return 1;

    /* hazard 2 (lava) or 3 (swamp): set timer and gustJarState bit 0 */
    entity->timer = 1;
    entity->gustJarState |= 1;
    return (s32)hazard;
}

/*
 * Hazard handler function table — gUnk_080012C8.
 * Index 0 = NULL (unused), 1 = hole, 2 = drown, 3 = lava, 4 = swamp.
 */
typedef void (*HazardFn)(Entity*);
static void HazardNull(Entity* e) {
    (void)e;
}

static HazardFn sHazardTable[5];
static int sHazardTableInit = 0;

static void InitHazardTable(void) {
    if (sHazardTableInit)
        return;
    sHazardTableInit = 1;
    sHazardTable[0] = HazardNull;
    /* sub_08001214 is defined below — forward-declare */
    extern void sub_08001214(Entity*);
    sHazardTable[1] = sub_08001214;
    sHazardTable[2] = CreateDrownFX;
    sHazardTable[3] = CreateLavaDrownFX;
    sHazardTable[4] = CreateSwampDrownFX;
}

/*
 * sub_08001290 — dispatch to hazard handler table.
 * Port of 0x08001290 from enemy.s.
 */
void sub_08001290(Entity* entity, u32 hazardType) {
    if (hazardType == 0)
        return;
    InitHazardTable();
    sHazardTable[hazardType](entity);
}

/*
 * sub_08001214 — Hole / pit fall handler.
 * Port of 0x08001214 from enemy.s.
 * Called each frame while an enemy is standing on a hole tile.
 */
void sub_08001214(Entity* entity) {
    if (!(entity->gustJarState & 1)) {
        /* First frame: initialise gustJarState and timer */
        entity->gustJarState = 1;
        u8 t = 1;
        if (entity->frame & 0x40) /* if frame bit 6 set → longer timer */
            t = 0x20;
        entity->timer = t;
    }
    /* Decrement timer each frame */
    entity->timer--;
    if (entity->timer == 0) {
        CreatePitFallFX(entity);
        return;
    }
    UpdateAnimationVariableFrames(entity, 4);
}

/*
 * EnemyFunctionHandler — main enemy action dispatcher with hazard check.
 * Port of 0x0800129E from enemy.s.
 *
 * Called by individual enemy functions to advance their state machine.
 *   entity  — the enemy being updated
 *   actions — pointer to the enemy's function table
 */
void EnemyFunctionHandler(Entity* entity, EntityActionArray actions) {
    s32 hazard = sub_080012DC(entity);
    void (*fn)(Entity*);

    if (hazard != 0) {
        InitHazardTable();
        fn = sHazardTable[hazard];
    } else {
        u32 idx = GetNextFunction(entity);
        fn = actions[idx];
    }
    fn(entity);
}

/*
 * GenericConfused — confusion state handler.
 * Port of 0x08001242 from enemy.s.
 * Called each frame by enemies when confused (stunned by Gust Jar, etc.).
 */
static const s8 sConfusedOffsets[] = { 0, 1, 0, -1 };

void GenericConfused(Entity* entity) {
    entity->confusedTime--;
    if (entity->confusedTime < 0x3C) {
        u8 phase = entity->confusedTime & 3;
        entity->spriteOffsetX = sConfusedOffsets[phase];
        if (entity->confusedTime == 0) {
            /* On GBA, a linked FX entity pointer is stored at offset 0x68
               (spanning field_0x68 + field_0x6a as 4 bytes). On 64-bit this
               doesn't work (pointer is 8 bytes), but the child field serves
               the same purpose in many cases. Check child for the confusion
               FX entity (kind=6, id=0xF, type=0x1C) and detach it. */
            Entity* fx = entity->child;
            if (fx != NULL && fx->kind == 6 && fx->id == 0xF && fx->type == 0x1C) {
                EnemyDetachFX(entity);
            }
        }
    }
    GravityUpdate(entity, 0x1800);
}

/*
 * GenericKnockback — trampoline to Knockback1.
 * Port of 0x08001324 from enemy.s.
 */
void GenericKnockback(Entity* entity) {
    Knockback1(entity);
}

/*
 * GenericKnockback2 — trampoline to Knockback2.
 * Port of 0x08001328 from enemy.s.
 */
void GenericKnockback2(Entity* entity) {
    Knockback2(entity);
}

/*
 * sub_08001318 — check health, then knockback.
 * Port of 0x08001318 from enemy.s.
 */
void sub_08001318(Entity* entity) {
    if (entity->z.HALF.HI < 0)
        entity->direction = 0xFF;
    Knockback1(entity);
}

/*
 * sub_0800132C — entity proximity check.
 * Port of 0x0800132C from enemy.s.
 * Returns the facing direction of entity toward target, or 0xFF if not close.
 */
extern u32 GetFacingDirection(Entity*, Entity*);

u32 sub_0800132C(Entity* entity, Entity* target) {
    /* Both must share at least one collision layer bit */
    if (!(entity->collisionLayer & target->collisionLayer))
        return 0xFF;
    /* Check distance: if either axis > 8px apart, return facing direction.
       If both axes within 8px (overlapping), return 0xFF (too close). */
    s32 dx = entity->x.HALF.HI - target->x.HALF.HI + 8;
    if ((u32)dx >= 0x11)
        return GetFacingDirection(entity, target);
    s32 dy = entity->y.HALF.HI - target->y.HALF.HI + 8;
    if ((u32)dy < 0x11)
        return 0xFF; /* overlapping — no direction */
    return GetFacingDirection(entity, target);
}

/*
 * EnemyUpdate — per-frame update for all enemies.
 * Port of 0x080011C4 from enemy.s.
 */
void EnemyUpdate(Entity* entity) {
    if (entity->action == 0) {
        if (!EnemyInit(entity)) {
            DeleteThisEntity();
            return; /* DeleteThisEntity may not return */
        }
        /* EnemyInit succeeded — fall through to function dispatch */
    } else {
        if (EntityDisabled(entity))
            goto draw;
        sub_080028E0(entity);
    }

    /* Function dispatch: check bit 4 of extended field at GBA offset 0x6D */
    {
        GenericEntity* ge = (GenericEntity*)entity;
        u8 f6d = ge->field_0x6c.HALF.HI;
        if (!(f6d & 0x10)) {
            gEnemyFunctions[entity->id](entity);
            entity->contactFlags &= ~CONTACT_NOW;
        }
    }

draw:
    DrawEntity(entity);
}