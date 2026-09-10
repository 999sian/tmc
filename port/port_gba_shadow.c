/*
 * port_gba_shadow.c — GBA-address shadow of the engine's native state.
 *
 * See port_gba_shadow.h for why this exists. Short version: RetroAchievements
 * sets for AGB are authored against a real GBA dump, so their conditions name
 * addresses like 0x02002A90. This port has no GBA memory image — the state
 * lives in native C globals — so we rebuild the interesting parts of EWRAM and
 * IWRAM once per frame, at the addresses linker.ld records for those globals.
 *
 * ---------------------------------------------------------------------------
 * The correctness gate
 * ---------------------------------------------------------------------------
 * A range may only be shadowed if its native byte layout is identical to
 * retail. The only thing that breaks layout on this port is pointer widening
 * (4 -> 8 bytes), which always grows a struct. So:
 *
 *   - Whole-object rows are gated on sizeof(native) == retail sizeof. Equal
 *     size ⇒ no widened pointer inside ⇒ memcpy is valid.
 *   - Structs that do contain pointers are shadowed only up to the last field
 *     before the first pointer ("prefix rows"), gated on that field sitting at
 *     its retail offset. Note the prefix ends at the field boundary, not at
 *     offsetof(first pointer): on LP64 the compiler inserts alignment padding
 *     in front of the pointer (e.g. FuseInfo.entity is at 0x0C on GBA but 0x10
 *     here), and that padding is not retail data.
 *   - Everything else is rejected and reads as zero. See sRejected below.
 *
 * The retail sizes/offsets asserted here were derived by compiling the very
 * same headers with 32-bit pointers and diffing; the numbers agree with the
 * independent retail sizes already recorded in the headers themselves (e.g.
 * PORT_STATIC_ASSERT_SIZE(Area, 0x894, 0xEC0) in include/area.h).
 *
 * ---------------------------------------------------------------------------
 * Cost
 * ---------------------------------------------------------------------------
 * 1888 bytes per frame across 15 rows (1569 EWRAM + 319 IWRAM), i.e. ~113 KB/s
 * at 60 fps. No dirty tracking: any hash or compare cheap enough to be worth it
 * would have to touch the same bytes the memcpy touches, so it cannot win.
 * No row is adjacent to another (the closest pair is gFadeControl+0x1C = 0xFEC
 * and gInput at 0xFF0), so coalescing would be a no-op and is not implemented.
 */

#include "global.h"

#include "area.h"
#include "common.h"
#include "entity.h"
#include "fade.h"
#include "kinstone.h"
#include "main.h"
#include "message.h"
#include "player.h"
#include "room.h"
#include "save.h"
#include "script.h"

#include "port_gba_shadow.h"
#include "port_types.h"

#include <stdio.h>
#include <string.h>

#define EWRAM_BASE 0x02000000u
#define EWRAM_SIZE 0x00040000u
#define IWRAM_BASE 0x03000000u
#define IWRAM_SIZE 0x00008000u

typedef struct {
    uint32_t offset; /* offset from the region base */
    const void* src; /* native object (or the head of it) */
    uint32_t size;   /* bytes proven layout-identical to retail */
} ShadowRow;

/* ------------------------------------------------------------------------- */
/* Compile-time size gate — whole-object rows.                               */
/* Left number is the retail GBA size; a mismatch means the struct picked up  */
/* a pointer and must move to the rejected list.                             */
/* ------------------------------------------------------------------------- */
PORT_STATIC_ASSERT(sizeof(gMessage) == 0x20, "gMessage layout differs from retail");
PORT_STATIC_ASSERT(sizeof(gSave) == 0x500, "gSave layout differs from retail");
PORT_STATIC_ASSERT(sizeof(gSmallChests) == 0x40, "gSmallChests layout differs from retail");
PORT_STATIC_ASSERT(sizeof(gActiveScriptInfo) == 0x0C, "gActiveScriptInfo layout differs from retail");
PORT_STATIC_ASSERT(sizeof(gManagerCount) == 0x01, "gManagerCount layout differs from retail");
PORT_STATIC_ASSERT(sizeof(gRoomTransition) == 0xB0, "gRoomTransition layout differs from retail");
PORT_STATIC_ASSERT(sizeof(gFadeControl) == 0x1C, "gFadeControl layout differs from retail");
PORT_STATIC_ASSERT(sizeof(gInput) == 0x08, "gInput layout differs from retail");
PORT_STATIC_ASSERT(sizeof(gMain) == 0x0E, "gMain layout differs from retail");
PORT_STATIC_ASSERT(sizeof(gEntCount) == 0x01, "gEntCount layout differs from retail");

/* ------------------------------------------------------------------------- */
/* Compile-time gate — prefix rows.                                          */
/* Each pair pins the last field inside the prefix to its retail offset and   */
/* keeps the first pointer out of the prefix.                                */
/* ------------------------------------------------------------------------- */
#define SHADOW_PREFIX_GATE(type, last_field, prefix_end, first_ptr)                   \
    PORT_STATIC_ASSERT(offsetof(type, last_field) < (prefix_end),                     \
                       #type ": " #last_field " is not inside the shadowed prefix");  \
    PORT_STATIC_ASSERT(offsetof(type, first_ptr) >= (prefix_end),                     \
                       #type ": shadowed prefix would include the pointer " #first_ptr)

/* FuseInfo: fusionState/kinstoneId/textIndex..., pointer `entity` at 0x0C. */
PORT_STATIC_ASSERT(offsetof(FuseInfo, fusingTextIndex) == 0x0A, "FuseInfo prefix layout differs from retail");
SHADOW_PREFIX_GATE(FuseInfo, fusingTextIndex, 0x0C, entity);

/* Area: dungeon_idx/localFlagOffset/flag_bank/portal state, then the
 * RoomResInfo array whose elements hold `void** properties`. */
PORT_STATIC_ASSERT(offsetof(Area, unk28) == 0x28, "Area prefix layout differs from retail");
PORT_STATIC_ASSERT(sizeof(struct_area_28) == 0x14, "Area prefix layout differs from retail");
SHADOW_PREFIX_GATE(Area, unk28, 0x3C, roomResInfos);

/* RoomVars: flags[52]/lightLevel/droptable/animFlags, then properties[8]. */
PORT_STATIC_ASSERT(offsetof(RoomVars, animFlags) == 0x68, "RoomVars prefix layout differs from retail");
SHADOW_PREFIX_GATE(RoomVars, animFlags, 0x6C, properties);

/* RoomControls: area/room/scroll/width/height, then `camera_target`. */
PORT_STATIC_ASSERT(offsetof(RoomControls, bg3OffsetY) == 0x2C, "RoomControls prefix layout differs from retail");
SHADOW_PREFIX_GATE(RoomControls, bg3OffsetY, 0x30, camera_target);

/* PlayerState: action/floor_type/swim_state/tilePos..., then `item`. */
PORT_STATIC_ASSERT(offsetof(PlayerState, field_0x27) == 0x27, "PlayerState prefix layout differs from retail");
SHADOW_PREFIX_GATE(PlayerState, field_0x27, 0x2C, item);

/* ------------------------------------------------------------------------- */
/* The table. Sorted by address; sortedness and non-overlap are checked by    */
/* the self-test.                                                            */
/* ------------------------------------------------------------------------- */
static const ShadowRow sEwramRows[] = {
    { 0x00050u, &gMessage, sizeof(gMessage) },                   /* current message box state */
    { 0x02A40u, &gSave, sizeof(gSave) },                         /* flags, inventory, figurines, kinstones, stats */
    { 0x17660u, &gSmallChests, sizeof(gSmallChests) },           /* opened-chest tracking for the room */
    { 0x22740u, &gFuseInfo, 0x0Cu },                             /* kinstone fusion in progress */
    { 0x33280u, &gActiveScriptInfo, sizeof(gActiveScriptInfo) }, /* running script id */
    { 0x33A90u, &gArea, 0x3Cu },                                 /* dungeon_idx, localFlagOffset, portal state */
    { 0x34350u, &gRoomVars, 0x6Cu },                             /* per-room flags, light level, droptable */
    { 0x354B4u, &gManagerCount, sizeof(gManagerCount) },         /* live manager count */
};

static const ShadowRow sIwramRows[] = {
    { 0x00BF0u, &gRoomControls, 0x30u },                       /* area, room, camera scroll, room size */
    { 0x00FD0u, &gFadeControl, sizeof(gFadeControl) },         /* fade/transition state */
    { 0x00FF0u, &gInput, sizeof(gInput) },                     /* held/pressed buttons */
    { 0x01000u, &gMain, sizeof(gMain) },                       /* game state + substate */
    { 0x010A0u, &gRoomTransition, sizeof(gRoomTransition) },   /* destination area/room during a transition */
    { 0x03DBCu, &gEntCount, sizeof(gEntCount) },               /* live entity count */
    { 0x03F80u, &gPlayerState, 0x2Cu },                        /* floor type, swim/dash state, tile position */
};

/* ------------------------------------------------------------------------- */
/* Rejected symbols. These sit in the achievement-interesting part of RAM but */
/* their native layout does not match retail, so their ranges read as zero.   */
/* Reviving one means finding a pointer-free prefix and adding a prefix row.  */
/* ------------------------------------------------------------------------- */
typedef struct {
    const char* symbol;
    const char* reason;
} ShadowReject;

static const ShadowReject sRejected[] = {
    { "gPlayerEntity", "Entity starts with prev/next pointers at 0x00: no pointer-free prefix (0x88 -> 0xB8)" },
    { "gEntities", "array of Entity; element stride grows 0x88 -> 0xB8, so no element lands at its retail address" },
    { "gAuxPlayerEntities", "array of Entity; element stride grows 0x88 -> 0xB8" },
    { "gCarriedEntity", "Entity-derived; leading prev/next pointers (0x8C -> 0x110)" },
    { "gEntityLists", "array of Entity* list heads: entirely pointers (0x48 -> 0x90)" },
    { "gEntityListsBackup", "array of Entity* list heads: entirely pointers" },
    { "gNPCData", "array of NPCStruct; element stride grows (0x1000 -> 0x1800)" },
    { "gActiveItems", "array of ItemBehavior; element stride grows (0x70 -> 0x80)" },
    { "gArea (past 0x3C)", "RoomResInfo[] holds void** properties; only the 0x3C-byte header is shadowed" },
    { "gRoomVars (past 0x6C)", "properties/entityRails/puzzleEntities are pointer arrays" },
    { "gRoomControls (past 0x30)", "camera_target is an Entity* at 0x30" },
    { "gPlayerState (past 0x2C)", "item is an Entity* at 0x2C; killed/skills sit behind it" },
    { "gFuseInfo (past 0x0C)", "entity is an Entity* at 0x0C" },
    { "gHUD", "contains pointers; grows 0x334 -> 0x4B8" },
    { "gUI", "contains pointers; grows 0x3B4 -> 0x480" },
    { "gGFXSlots", "contains pointers; grows 0x214 -> 0x2C8" },
    { "gScreen", "contains pointers; grows 0x7C -> 0xA0" },
    { "gTextRender", "contains pointers; grows 0xA8 -> 0xD8" },
    { "gBgAnimations", "contains pointers; grows 0x40 -> 0x80" },
    { "gPriorityHandler", "contains pointers; grows 0x0C -> 0x18" },
    { "gPossibleInteraction", "contains pointers; grows 0x188 -> 0x310" },
    { "gCurrentRoomMemory", "is itself a pointer (RoomMemory*): value is a native address, meaningless as GBA data" },
    { "gCurrentRoomProperties", "is itself a pointer (void**)" },
    { "gPlayerClones", "array of Entity*" },
    { "gRoomMemory", "declared as an incomplete array (RoomMemory gRoomMemory[]): no sizeof to gate on" },
    { "gMenu / gIntroState / gChooseFileState", "all three alias 0x02000080; which one is live is a runtime "
                                                "property, so no single row is correct" },
    { "gDiggingCaveEntranceTransition", "contains a pointer; grows 0x0C -> 0x10" },
    { "gOAMControls / gPaletteList / gBG*Buffer / gMapTop / gMapBottom / gDungeonMap",
      "graphics scratch, not game state: the port renders natively and never fills these" },
};

/* ------------------------------------------------------------------------- */

static u8 sEwram[EWRAM_SIZE];
static u8 sIwram[IWRAM_SIZE];

static void RefreshRegion(u8* dst, const ShadowRow* rows, size_t count) {
    for (size_t i = 0; i < count; i++)
        memcpy(dst + rows[i].offset, rows[i].src, rows[i].size);
}

void Port_GbaShadow_Refresh(void) {
#ifdef PORT_GBA_SHADOW_SELFTEST
    static bool sSelfTested = false;
    if (!sSelfTested) {
        sSelfTested = true;
        Port_GbaShadow_SelfTest();
    }
#endif
    RefreshRegion(sEwram, sEwramRows, ARRAY_COUNT(sEwramRows));
    RefreshRegion(sIwram, sIwramRows, ARRAY_COUNT(sIwramRows));
}

uint32_t Port_GbaShadow_Read(uint32_t gba_addr, uint8_t* out, uint32_t n) {
    const u8* base;
    uint32_t offset;
    uint32_t avail;

    if (out == NULL || n == 0)
        return 0;

    if (gba_addr >= EWRAM_BASE && gba_addr < EWRAM_BASE + EWRAM_SIZE) {
        base = sEwram;
        offset = gba_addr - EWRAM_BASE;
        avail = EWRAM_SIZE - offset;
    } else if (gba_addr >= IWRAM_BASE && gba_addr < IWRAM_BASE + IWRAM_SIZE) {
        base = sIwram;
        offset = gba_addr - IWRAM_BASE;
        avail = IWRAM_SIZE - offset;
    } else {
        /* Not shadowed (cart SRAM, VRAM, ROM, or the gap between the regions).
         * EWRAM and IWRAM are not contiguous, so a read that runs off the end
         * of one is truncated here rather than continuing into the other. */
        return 0;
    }

    if (n > avail)
        n = avail;
    memcpy(out, base + offset, n);
    return n;
}

void Port_GbaShadow_GetStats(int* shadowed, int* rejected, uint32_t* bytes) {
    if (shadowed != NULL)
        *shadowed = (int)(ARRAY_COUNT(sEwramRows) + ARRAY_COUNT(sIwramRows));
    if (rejected != NULL)
        *rejected = (int)ARRAY_COUNT(sRejected);
    if (bytes != NULL) {
        uint32_t total = 0;
        for (size_t i = 0; i < ARRAY_COUNT(sEwramRows); i++)
            total += sEwramRows[i].size;
        for (size_t i = 0; i < ARRAY_COUNT(sIwramRows); i++)
            total += sIwramRows[i].size;
        *bytes = total;
    }
}

const char* Port_GbaShadow_GetRejected(int index, const char** reason) {
    if (index < 0 || (size_t)index >= ARRAY_COUNT(sRejected))
        return NULL;
    if (reason != NULL)
        *reason = sRejected[index].reason;
    return sRejected[index].symbol;
}

/* ------------------------------------------------------------------------- */
/* Self-test                                                                 */
/* ------------------------------------------------------------------------- */

#define SHADOW_CHECK(cond)                                                            \
    do {                                                                              \
        if (!(cond)) {                                                                \
            fprintf(stderr, "Port_GbaShadow_SelfTest: %s:%d: %s\n", __FILE__,         \
                    __LINE__, #cond);                                                 \
            ok = false;                                                               \
        }                                                                             \
    } while (0)

static bool CheckRegionRows(const ShadowRow* rows, size_t count, uint32_t region_size) {
    bool ok = true;
    uint32_t prev_end = 0;
    for (size_t i = 0; i < count; i++) {
        SHADOW_CHECK(rows[i].src != NULL);
        SHADOW_CHECK(rows[i].size > 0);
        SHADOW_CHECK(rows[i].offset >= prev_end); /* sorted and non-overlapping */
        SHADOW_CHECK(rows[i].offset < region_size);
        SHADOW_CHECK(rows[i].size <= region_size - rows[i].offset);
        prev_end = rows[i].offset + rows[i].size;
    }
    return ok;
}

bool Port_GbaShadow_SelfTest(void) {
    /* GBA addresses from linker.ld: gSave = 0x02002A40, gRoomControls = 0x03000BF0. */
    const uint32_t kSaveAddr = 0x02002A40u;
    const uint32_t kRoomControlsAddr = 0x03000BF0u;
    SaveFile saved_save;
    RoomControls saved_room;
    u8 buf[16];
    bool ok = true;

    ok &= CheckRegionRows(sEwramRows, ARRAY_COUNT(sEwramRows), EWRAM_SIZE);
    ok &= CheckRegionRows(sIwramRows, ARRAY_COUNT(sIwramRows), IWRAM_SIZE);

    memcpy(&saved_save, &gSave, sizeof(gSave));
    memcpy(&saved_room, &gRoomControls, sizeof(gRoomControls));

    /* Sentinels at known retail offsets inside gSave. */
    gSave.global_progress = 0x5Au;  /* 0x008 */
    gSave.enemies_killed = 0xDEADBEEFu; /* 0x050 */
    gSave.flags[0x1FF] = 0xA5u;     /* 0x25B + 0x1FF, i.e. the last flag byte */
    gRoomControls.area = 0x33u;     /* 0x004 */
    gRoomControls.room = 0x07u;     /* 0x005 */

    Port_GbaShadow_Refresh();

    /* Byte-exact readback at the GBA address, and the served count. */
    memset(buf, 0, sizeof(buf));
    SHADOW_CHECK(Port_GbaShadow_Read(kSaveAddr + 0x008u, buf, 1) == 1);
    SHADOW_CHECK(buf[0] == 0x5Au);

    memset(buf, 0, sizeof(buf));
    SHADOW_CHECK(Port_GbaShadow_Read(kSaveAddr + 0x050u, buf, 4) == 4);
    SHADOW_CHECK(buf[0] == 0xEFu && buf[1] == 0xBEu && buf[2] == 0xADu && buf[3] == 0xDEu);

    memset(buf, 0, sizeof(buf));
    SHADOW_CHECK(Port_GbaShadow_Read(kSaveAddr + 0x25Bu + 0x1FFu, buf, 1) == 1);
    SHADOW_CHECK(buf[0] == 0xA5u);

    memset(buf, 0, sizeof(buf));
    SHADOW_CHECK(Port_GbaShadow_Read(kRoomControlsAddr + 0x004u, buf, 2) == 2);
    SHADOW_CHECK(buf[0] == 0x33u && buf[1] == 0x07u);

    /* Offsets must be exact: one byte earlier is a different field. */
    memset(buf, 0, sizeof(buf));
    SHADOW_CHECK(Port_GbaShadow_Read(kSaveAddr + 0x007u, buf, 1) == 1);
    SHADOW_CHECK(buf[0] != 0x5Au);

    /* Unmapped and rejected ranges read as zero, and never fail. */
    memset(buf, 0xFFu, sizeof(buf));
    SHADOW_CHECK(Port_GbaShadow_Read(0x02033A90u + 0x3Cu, buf, 4) == 4); /* gArea past its prefix */
    SHADOW_CHECK(buf[0] == 0 && buf[1] == 0 && buf[2] == 0 && buf[3] == 0);

    /* Region ends truncate; out-of-region and degenerate reads serve nothing. */
    SHADOW_CHECK(Port_GbaShadow_Read(EWRAM_BASE + EWRAM_SIZE - 4u, buf, 16) == 4);
    SHADOW_CHECK(Port_GbaShadow_Read(IWRAM_BASE + IWRAM_SIZE - 4u, buf, 16) == 4);
    SHADOW_CHECK(Port_GbaShadow_Read(EWRAM_BASE + EWRAM_SIZE, buf, 4) == 0);
    SHADOW_CHECK(Port_GbaShadow_Read(IWRAM_BASE + IWRAM_SIZE, buf, 4) == 0);
    SHADOW_CHECK(Port_GbaShadow_Read(EWRAM_BASE - 1u, buf, 4) == 0);
    SHADOW_CHECK(Port_GbaShadow_Read(0x0E000000u, buf, 4) == 0); /* cart SRAM: not shadowed */
    SHADOW_CHECK(Port_GbaShadow_Read(kSaveAddr, buf, 0) == 0);
    SHADOW_CHECK(Port_GbaShadow_Read(kSaveAddr, NULL, 4) == 0);

    memcpy(&gSave, &saved_save, sizeof(gSave));
    memcpy(&gRoomControls, &saved_room, sizeof(gRoomControls));
    Port_GbaShadow_Refresh();

    return ok;
}
