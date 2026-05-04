/*
 * port_quicksave.c — F5 quicksave / F6 quickload.
 *
 * Snapshots a curated set of game-state regions into a heap buffer on
 * F5, then memcpys them back on F6. Keeping this in C so we can name the
 * game globals directly; the C++ debug-menu calls these via the small
 * extern "C" API.
 *
 * Coverage: emulated GBA memory (EWRAM/IWRAM/VRAM/IO), the save file,
 * the player + state, the room controls + transition, gMain, and the
 * full gEntities array. Anything not in this list (HUD state, OAM, gfx
 * slots, palette buffers) will visually catch up over the next frame.
 *
 * Caveats:
 *  - Snapshotting mid-frame is supported but the visible result is
 *    "next frame" — entity logic that ran this frame may have already
 *    written to OAM, which is not snapshotted.
 *  - This does NOT save to disk. The snapshot lives in the process
 *    memory and is lost when the game exits.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "structures.h"
#include "save.h"
#include "main.h"
#include "entity.h"
#include "port_gba_mem.h"

extern u8 gEwram[];
extern u8 gIwram[];
extern u8 gVram[];
extern u8 gIoMem[];

typedef struct {
    void* ptr;
    size_t size;
    const char* name;
} StateRegion;

/* List of regions captured by F5. The order doesn't matter for save,
 * but for restore the order also doesn't matter as long as the regions
 * don't overlap — they don't. */
static StateRegion sRegions[] = {
    { gEwram, 0x40000, "gEwram" },
    { gIwram, 0x8000,  "gIwram" },
    { gVram,  0x18000, "gVram"  },
    { gIoMem, 0x400,   "gIoMem" },
    { &gSave,           sizeof(gSave),           "gSave" },
    { &gPlayerEntity,   sizeof(gPlayerEntity),   "gPlayerEntity" },
    { &gPlayerState,    sizeof(gPlayerState),    "gPlayerState" },
    { &gMain,           sizeof(gMain),           "gMain" },
    { &gRoomControls,   sizeof(gRoomControls),   "gRoomControls" },
    { &gRoomTransition, sizeof(gRoomTransition), "gRoomTransition" },
    { gEntities,        sizeof(gEntities),       "gEntities" },
};

#define NUM_REGIONS (sizeof(sRegions) / sizeof(sRegions[0]))

static u8* sSnapshot = NULL;
static size_t sSnapshotBytes = 0;
static int sSnapshotValid = 0;

static size_t TotalRegionBytes(void) {
    size_t total = 0;
    for (size_t i = 0; i < NUM_REGIONS; i++) {
        total += sRegions[i].size;
    }
    return total;
}

int Port_QuickSave(void) {
    size_t total = TotalRegionBytes();
    if (sSnapshot == NULL) {
        sSnapshot = (u8*)malloc(total);
        if (sSnapshot == NULL) {
            fprintf(stderr, "[quicksave] failed to allocate %zu bytes\n", total);
            return 0;
        }
        sSnapshotBytes = total;
    } else if (sSnapshotBytes != total) {
        /* Region list changed between calls — should not happen. */
        free(sSnapshot);
        sSnapshot = (u8*)malloc(total);
        if (sSnapshot == NULL) {
            sSnapshotBytes = 0;
            return 0;
        }
        sSnapshotBytes = total;
    }

    u8* dst = sSnapshot;
    for (size_t i = 0; i < NUM_REGIONS; i++) {
        memcpy(dst, sRegions[i].ptr, sRegions[i].size);
        dst += sRegions[i].size;
    }
    sSnapshotValid = 1;
    fprintf(stderr, "[quicksave] saved %zu bytes\n", total);
    return 1;
}

int Port_QuickLoad(void) {
    if (!sSnapshotValid || sSnapshot == NULL) {
        return 0;
    }
    const u8* src = sSnapshot;
    for (size_t i = 0; i < NUM_REGIONS; i++) {
        memcpy(sRegions[i].ptr, src, sRegions[i].size);
        src += sRegions[i].size;
    }
    fprintf(stderr, "[quicksave] restored %zu bytes\n", sSnapshotBytes);
    return 1;
}

int Port_QuickSave_HasSnapshot(void) {
    return sSnapshotValid;
}
