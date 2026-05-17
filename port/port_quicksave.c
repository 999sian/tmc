/*
 * port_quicksave.c — multi-slot save-states with disk persistence + auto-save.
 *
 * Snapshots a curated set of game-state regions into an in-memory slot
 * on save, restores them on load. Slots also get serialized to disk as
 * `state_<slot>.bin` next to the binary so they survive restart.
 *
 * Slot layout:
 *   slot 0:    F5 quicksave / F6 quickload (legacy single-slot API)
 *   slot 1-4:  F1..F4 load,  Shift+F1..F4 save (numbered manual slots)
 *   slot 5-7:  auto-save ring (Port_QuickSave_Auto cycles through these)
 *
 * File format (disk persistence):
 *   magic    "TMCS"                          (4 bytes)
 *   version  PORT_QUICKSAVE_VERSION          (u32 LE)
 *   total    sum of all region sizes         (u32 LE)
 *   data     concatenated region bytes       (in sRegions[] order)
 *
 * On load, if magic/version/size don't match, the file is rejected
 * silently — the in-memory snapshot (if any) stays untouched. This is
 * defensive against schema changes between builds; saves are best-effort,
 * not a contract.
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
 *  - Save-states are not the same as the game's in-engine save file
 *    (`tmc.sav`). The game's own save still goes through its file-select
 *    flow. Save-states capture transient runtime state including
 *    mid-cutscene positions.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include <SDL3/SDL.h>

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

/* List of regions captured by a save-state. The order doesn't matter for
 * save, but for restore the order must stay consistent with what was on
 * disk — which is why the disk format records the total size and we
 * reject files that don't match the current region layout. */
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

#define NUM_REGIONS  (sizeof(sRegions) / sizeof(sRegions[0]))
#define NUM_SLOTS    8        /* 0..4 manual + 5..7 auto-save ring */
#define AUTO_SLOT_BASE 5
#define NUM_AUTO_SLOTS 3
#define MAGIC      0x53434D54u /* "TMCS" little-endian */
#define VERSION    1u

typedef struct {
    u8*     snapshot;       /* heap, NULL if slot empty */
    size_t  bytes;
    int     valid;
    u64     saved_at_unix;  /* clock_gettime CLOCK_REALTIME seconds */
} Slot;

static Slot sSlots[NUM_SLOTS];
static int  sAutoNextSlot = AUTO_SLOT_BASE;   /* round-robin cursor */
static u64  sAutoLastSaveTicksMs = 0;
static int  sAutoEnabled = 1;                 /* on by default — the F8
                                                 toggle (and config.json)
                                                 can flip it off. */
static u32  sAutoIntervalMs = 60000;          /* 60 seconds default */

/* Area-change auto-save (independent of the interval timer). Tracks
 * the last-observed (area, room) and saves to the ring whenever it
 * changes. Helps with the crash-on-load-then-lose-an-hour case Jester
 * flagged. */
static int  sAutoOnAreaChange = 1;
static u8   sLastSeenArea = 0xFF;
static u8   sLastSeenRoom = 0xFF;

static size_t TotalRegionBytes(void) {
    size_t total = 0;
    for (size_t i = 0; i < NUM_REGIONS; i++) {
        total += sRegions[i].size;
    }
    return total;
}

static int Snapshot_Capture(Slot* s) {
    const size_t total = TotalRegionBytes();
    if (s->snapshot == NULL || s->bytes != total) {
        free(s->snapshot);
        s->snapshot = (u8*)malloc(total);
        if (s->snapshot == NULL) {
            s->bytes = 0;
            s->valid = 0;
            fprintf(stderr, "[quicksave] alloc failed (%zu bytes)\n", total);
            return 0;
        }
        s->bytes = total;
    }
    u8* dst = s->snapshot;
    for (size_t i = 0; i < NUM_REGIONS; i++) {
        memcpy(dst, sRegions[i].ptr, sRegions[i].size);
        dst += sRegions[i].size;
    }
    s->valid = 1;
    s->saved_at_unix = (u64)time(NULL);
    return 1;
}

static int Snapshot_Restore(const Slot* s) {
    if (!s->valid || s->snapshot == NULL || s->bytes != TotalRegionBytes()) {
        return 0;
    }
    const u8* src = s->snapshot;
    for (size_t i = 0; i < NUM_REGIONS; i++) {
        memcpy(sRegions[i].ptr, src, sRegions[i].size);
        src += sRegions[i].size;
    }
    return 1;
}

static void SlotFilename(int slot, char* out, size_t cap) {
    if (slot >= AUTO_SLOT_BASE) {
        snprintf(out, cap, "state_auto_%d.bin", slot - AUTO_SLOT_BASE);
    } else if (slot == 0) {
        snprintf(out, cap, "state_quick.bin");
    } else {
        snprintf(out, cap, "state_%d.bin", slot);
    }
}

static int WriteSlotToDisk(int slot) {
    if (slot < 0 || slot >= NUM_SLOTS) return 0;
    Slot* s = &sSlots[slot];
    if (!s->valid || s->snapshot == NULL) return 0;

    char path[64];
    SlotFilename(slot, path, sizeof(path));
    FILE* f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "[quicksave] open %s for write failed\n", path);
        return 0;
    }
    const u32 magic = MAGIC;
    const u32 version = VERSION;
    const u32 total = (u32)s->bytes;
    const u64 saved_at = s->saved_at_unix;
    fwrite(&magic, sizeof(magic), 1, f);
    fwrite(&version, sizeof(version), 1, f);
    fwrite(&total, sizeof(total), 1, f);
    fwrite(&saved_at, sizeof(saved_at), 1, f);
    const size_t written = fwrite(s->snapshot, 1, s->bytes, f);
    fclose(f);
    if (written != s->bytes) {
        fprintf(stderr, "[quicksave] short write %s (%zu/%zu)\n", path, written, s->bytes);
        return 0;
    }
    return 1;
}

static int ReadSlotFromDisk(int slot) {
    if (slot < 0 || slot >= NUM_SLOTS) return 0;
    char path[64];
    SlotFilename(slot, path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    u32 magic = 0, version = 0, total = 0;
    u64 saved_at = 0;
    if (fread(&magic, sizeof(magic), 1, f) != 1 ||
        fread(&version, sizeof(version), 1, f) != 1 ||
        fread(&total, sizeof(total), 1, f) != 1 ||
        fread(&saved_at, sizeof(saved_at), 1, f) != 1) {
        fclose(f);
        return 0;
    }
    if (magic != MAGIC || version != VERSION || total != (u32)TotalRegionBytes()) {
        fclose(f);
        return 0;
    }
    Slot* s = &sSlots[slot];
    if (s->snapshot == NULL || s->bytes != total) {
        free(s->snapshot);
        s->snapshot = (u8*)malloc(total);
        if (!s->snapshot) {
            s->bytes = 0;
            s->valid = 0;
            fclose(f);
            return 0;
        }
        s->bytes = total;
    }
    const size_t got = fread(s->snapshot, 1, total, f);
    fclose(f);
    if (got != total) {
        s->valid = 0;
        return 0;
    }
    s->valid = 1;
    s->saved_at_unix = saved_at;
    return 1;
}

/* ============================================================
 *   Public API
 * ============================================================ */

int Port_QuickSave_SaveSlot(int slot) {
    if (slot < 0 || slot >= NUM_SLOTS) return 0;
    if (!Snapshot_Capture(&sSlots[slot])) return 0;
    /* Best-effort disk persistence — failure is non-fatal, the in-memory
     * snapshot still works for the session. */
    WriteSlotToDisk(slot);
    fprintf(stderr, "[quicksave] slot %d saved (%zu bytes)\n", slot, sSlots[slot].bytes);
    return 1;
}

int Port_QuickSave_LoadSlot(int slot) {
    if (slot < 0 || slot >= NUM_SLOTS) return 0;
    if (!sSlots[slot].valid) {
        /* Try loading from disk first — handles "fresh launch, never
         * saved this session but slot exists on disk from last run". */
        if (!ReadSlotFromDisk(slot)) {
            fprintf(stderr, "[quicksave] slot %d empty\n", slot);
            return 0;
        }
    }
    if (!Snapshot_Restore(&sSlots[slot])) {
        fprintf(stderr, "[quicksave] slot %d restore failed\n", slot);
        return 0;
    }
    fprintf(stderr, "[quicksave] slot %d restored\n", slot);
    {
        /* Tell the Reborn-parity layer a resume just happened so it
         * can swallow the next queued Ezlo hint (if that toggle is on). */
        extern void Port_Reborn_NotifyJustResumed(void);
        Port_Reborn_NotifyJustResumed();
    }
    return 1;
}

int Port_QuickSave_HasSlot(int slot) {
    if (slot < 0 || slot >= NUM_SLOTS) return 0;
    if (sSlots[slot].valid) return 1;
    /* Probe disk so the menu can label populated-on-disk slots correctly
     * before the user touches them. */
    char path[64];
    SlotFilename(slot, path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

u64 Port_QuickSave_SlotTimestamp(int slot) {
    if (slot < 0 || slot >= NUM_SLOTS) return 0;
    if (sSlots[slot].valid) return sSlots[slot].saved_at_unix;
    /* Probe the disk file's timestamp header so the menu can show
     * "last saved" even for slots that haven't been loaded into memory. */
    char path[64];
    SlotFilename(slot, path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    u32 magic = 0, version = 0, total = 0;
    u64 saved_at = 0;
    if (fread(&magic, sizeof(magic), 1, f) == 1 &&
        fread(&version, sizeof(version), 1, f) == 1 &&
        fread(&total, sizeof(total), 1, f) == 1 &&
        fread(&saved_at, sizeof(saved_at), 1, f) == 1) {
        fclose(f);
        return saved_at;
    }
    fclose(f);
    return 0;
}

/* Legacy single-slot API — slot 0 is the F5/F6 quicksave. */
int Port_QuickSave(void)         { return Port_QuickSave_SaveSlot(0); }
int Port_QuickLoad(void)         { return Port_QuickSave_LoadSlot(0); }
int Port_QuickSave_HasSnapshot(void) { return Port_QuickSave_HasSlot(0); }

/* Auto-save — call once per frame from VBlankIntrWait. Saves to the
 * next slot in the auto-save ring if enabled and the configured
 * interval has elapsed since the last auto-save. */
static void TakeAutoSnapshot(const char* reason) {
    const int slot = sAutoNextSlot;
    sAutoNextSlot++;
    if (sAutoNextSlot >= AUTO_SLOT_BASE + NUM_AUTO_SLOTS) {
        sAutoNextSlot = AUTO_SLOT_BASE;
    }
    if (Port_QuickSave_SaveSlot(slot)) {
        fprintf(stderr, "[autosave] saved to ring slot %d (%s)\n", slot, reason);
    }
}

void Port_QuickSave_AutoTick(void) {
    if (!sAutoEnabled) return;
    const u64 now = SDL_GetTicks();

    /* Area-change trigger. gRoomControls is the engine's source-of-
     * truth for the current area/room; we just compare against the
     * last value we observed and fire a snapshot on transition. The
     * first poll seeds the cache without saving (sLastSeen* both
     * 0xFF) so we don't double-save on boot. */
    if (sAutoOnAreaChange) {
        /* gRoomControls is declared in include/room.h, already in scope
         * via include/save.h above. */
        const u8 area = gRoomControls.area;
        const u8 room = gRoomControls.room;
        if (sLastSeenArea == 0xFF && sLastSeenRoom == 0xFF) {
            sLastSeenArea = area;
            sLastSeenRoom = room;
        } else if (area != sLastSeenArea || room != sLastSeenRoom) {
            sLastSeenArea = area;
            sLastSeenRoom = room;
            sAutoLastSaveTicksMs = now;
            TakeAutoSnapshot("area-change");
            return;
        }
    }

    /* Interval trigger. */
    if (sAutoLastSaveTicksMs == 0) {
        sAutoLastSaveTicksMs = now;
        return;
    }
    if (now - sAutoLastSaveTicksMs < sAutoIntervalMs) return;
    sAutoLastSaveTicksMs = now;
    TakeAutoSnapshot("interval");
}

int Port_QuickSave_AutoOnAreaChangeEnabled(void) { return sAutoOnAreaChange; }
void Port_QuickSave_SetAutoOnAreaChange(int on) {
    sAutoOnAreaChange = on ? 1 : 0;
    /* Reset the seen-area cache when toggled on so the next change
     * doesn't fire spuriously against pre-toggle history. */
    if (on) {
        sLastSeenArea = 0xFF;
        sLastSeenRoom = 0xFF;
    }
}

void Port_QuickSave_SetAutoEnabled(int enabled) {
    sAutoEnabled = enabled ? 1 : 0;
    if (enabled) sAutoLastSaveTicksMs = SDL_GetTicks();
}

int Port_QuickSave_AutoEnabled(void) { return sAutoEnabled; }

void Port_QuickSave_SetAutoIntervalMs(u32 ms) {
    if (ms < 5000) ms = 5000;       /* clamp to 5s minimum — anything
                                       faster would thrash on busy
                                       frames and risk visible hitches. */
    if (ms > 600000) ms = 600000;   /* 10 minute cap */
    sAutoIntervalMs = ms;
}

u32 Port_QuickSave_AutoIntervalMs(void) { return sAutoIntervalMs; }

int Port_QuickSave_SlotCount(void)     { return NUM_SLOTS; }
int Port_QuickSave_AutoSlotBase(void)  { return AUTO_SLOT_BASE; }
int Port_QuickSave_AutoSlotCount(void) { return NUM_AUTO_SLOTS; }
