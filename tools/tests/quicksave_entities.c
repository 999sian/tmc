#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>

static int sFailFlush, sFailSync, sFailClose, sFailRename;
static int TestFlush(FILE* f) {
    if (sFailFlush) { errno = ENOSPC; return EOF; }
    return fflush(f);
}
static int TestSync(int fd) {
    if (sFailSync) { errno = EIO; return -1; }
    return fsync(fd);
}
static int TestClose(FILE* f) {
    int result = fclose(f);
    if (sFailClose) { errno = EIO; return EOF; }
    return result;
}
static int TestRename(const char* from, const char* to) {
    if (sFailRename) { errno = EACCES; return -1; }
    return rename(from, to);
}
#define fflush TestFlush
#define fsync TestSync
#define fclose TestClose
#define rename TestRename
#include "port/port_quicksave.c"
#undef fflush
#undef fsync
#undef fclose
#undef rename
#include <assert.h>
u8 gEwram[0x40000], gIwram[0x8000], gVram[0x18000], gIoMem[0x400];
u32 gRand;
u64 gPracticeFrame;
#define DEFINE_GLOBAL(name) __typeof__(name) name
DEFINE_GLOBAL(gSave); DEFINE_GLOBAL(gPlayerEntity); DEFINE_GLOBAL(gPlayerState);
DEFINE_GLOBAL(gMain); DEFINE_GLOBAL(gRoomControls); DEFINE_GLOBAL(gRoomTransition);
DEFINE_GLOBAL(gEntities); DEFINE_GLOBAL(gEntityLists); DEFINE_GLOBAL(gAuxPlayerEntities);
DEFINE_GLOBAL(gCarriedEntity); DEFINE_GLOBAL(gEntCount); DEFINE_GLOBAL(gManagerCount);
DEFINE_GLOBAL(gActiveItems);
LinkedList gEntityListsBackup[9];
Entity* gPlayerClones[3];
UpdateContext gUpdateContext;
void Port_RestorePlayerHitbox(void) {}

static void CheckFailedOverwrite(void) {
    assert(Port_QuickSave_SaveSlot(0));
    struct stat old;
    assert(stat("state_quick.bin", &old) == 0);
    unsigned char* original = malloc(old.st_size);
    unsigned char* actual = malloc(old.st_size);
    assert(original && actual);
    FILE* f = fopen("state_quick.bin", "rb");
    assert(f && fread(original, 1, old.st_size, f) == (size_t)old.st_size);
    assert(fclose(f) == 0);
    struct rlimit limit;
    assert(getrlimit(RLIMIT_FSIZE, &limit) == 0);
    void (*oldSignal)(int) = signal(SIGXFSZ, SIG_IGN);
    assert(oldSignal != SIG_ERR);
    enum { SHORT_WRITE, DISK_FULL, FLUSH, SYNC, CLOSE, RENAME };
    for (int failure = SHORT_WRITE; failure <= RENAME; ++failure) {
        if (failure <= DISK_FULL) {
            struct rlimit full = limit;
            full.rlim_cur = failure == SHORT_WRITE ? 128 : old.st_size - 1;
            assert(setrlimit(RLIMIT_FSIZE, &full) == 0);
        }
        sFailFlush = failure == FLUSH;
        sFailSync = failure == SYNC;
        sFailClose = failure == CLOSE;
        sFailRename = failure == RENAME;
        ++gRand;
        assert(!Port_QuickSave_SaveSlot(0));
        assert(setrlimit(RLIMIT_FSIZE, &limit) == 0);
        sFailFlush = sFailSync = sFailClose = sFailRename = 0;
        struct stat now;
        assert(stat("state_quick.bin", &now) == 0 && now.st_size == old.st_size);
        f = fopen("state_quick.bin", "rb");
        assert(f && fread(actual, 1, old.st_size, f) == (size_t)old.st_size);
        assert(fclose(f) == 0);
        assert(memcmp(original, actual, old.st_size) == 0);
        assert(access("state_quick.bin.tmp", F_OK) != 0);
        free(sSlots[0].snapshot);
        memset(&sSlots[0], 0, sizeof(sSlots[0]));
        assert(ReadSlotFromDisk(0));
    }
    assert(signal(SIGXFSZ, oldSignal) != SIG_ERR);
    assert(Port_QuickSave_SaveSlot(0));
    free(original);
    free(actual);
}

int main(void) {
    Slot s = {0};
    Entity* saved = &gEntities[0].base;
    for (int i = 0; i < 9; ++i)
        gEntityLists[i].first = gEntityLists[i].last = (Entity*)&gEntityLists[i];
    gEntityLists[0].first = gEntityLists[0].last = saved;
    saved->next = saved->prev = (Entity*)&gEntityLists[0];
    gEntCount = 1;
    gManagerCount = 2;
    memcpy(gEntityListsBackup, gEntityLists, sizeof(gEntityLists));
    gActiveItems[0].priority = 3;
    assert(Snapshot_Capture(&s));
    gEntityLists[0].first = gEntityLists[0].last = &gEntities[1].base;
    gEntCount = 20;
    gManagerCount = 0;
    memset(gEntityListsBackup, 0, sizeof(gEntityListsBackup));
    memset(gActiveItems, 0, sizeof(gActiveItems));
    assert(Snapshot_Restore(&s));
    assert(gEntityLists[0].first == saved && gEntityLists[0].last == saved);
    assert(saved->next == (Entity*)&gEntityLists[0] && saved->prev == (Entity*)&gEntityLists[0]);
    assert(gEntCount == 1 && gManagerCount == 2);
    assert(gEntityListsBackup[0].first == saved);
    assert(gActiveItems[0].priority == 3);
    free(s.snapshot);

    /* Simulate a previous process's addresses without ever dereferencing
     * them. Disk load must relocate nodes, sentinels and player references. */
    const uintptr_t delta = (uintptr_t)0x1000000000ULL;
#define OLD(ptr) ((Entity*)((uintptr_t)(ptr) + delta))
    for (int i = 0; i < 9; ++i)
        gEntityLists[i].first = gEntityLists[i].last = OLD(&gEntityLists[i]);
    gEntityLists[0].first = gEntityLists[0].last = OLD(saved);
    saved->next = saved->prev = OLD(&gEntityLists[0]);
    saved->parent = OLD(&gPlayerEntity);
    gPlayerClones[0] = OLD(&gAuxPlayerEntities[0]);
    gAuxPlayerEntities[0].base.parent = OLD(saved);
    gCarriedEntity.unk_8 = OLD(saved);
    gRoomControls.camera_target = OLD(saved);
    memcpy(gEntityListsBackup, gEntityLists, sizeof(gEntityLists));
    assert(Snapshot_Capture(&sSlots[0]));
    for (size_t i = 0; i < NUM_REGIONS; ++i)
        sSlots[0].saved_bases[i] += delta;
    assert(WriteSlotToDisk(0));
    free(sSlots[0].snapshot);
    memset(&sSlots[0], 0, sizeof(sSlots[0]));
    assert(ReadSlotFromDisk(0));
    assert(Snapshot_Restore(&sSlots[0]));
    assert(gEntityLists[0].first == saved && gEntityLists[0].last == saved);
    assert(saved->next == (Entity*)&gEntityLists[0]);
    assert(saved->prev == (Entity*)&gEntityLists[0]);
    assert(saved->parent == &gPlayerEntity.base);
    assert(gEntityLists[1].first == (Entity*)&gEntityLists[1]);
    assert(gEntityListsBackup[0].first == saved);
    assert(gPlayerClones[0] == &gAuxPlayerEntities[0].base);
    assert(gAuxPlayerEntities[0].base.parent == saved);
    assert(gCarriedEntity.unk_8 == saved);
    assert(gRoomControls.camera_target == saved);
    /* Re-saving a disk-loaded slot must replace its old address metadata. */
    assert(Snapshot_Capture(&sSlots[0]));
    assert(sSlots[0].saved_bases[0] == (uintptr_t)gEwram);
    assert(Snapshot_Restore(&sSlots[0]));
    assert(saved->next == (Entity*)&gEntityLists[0]);
    free(sSlots[0].snapshot);
    memset(&sSlots[0], 0, sizeof(sSlots[0]));
    CheckFailedOverwrite();
    free(sSlots[0].snapshot);
    memset(&sSlots[0], 0, sizeof(sSlots[0]));
    FILE* f = fopen("state_quick.bin", "r+b");
    const u32 oldVersion = 6;
    assert(f && fseek(f, sizeof(u32), SEEK_SET) == 0);
    assert(fwrite(&oldVersion, sizeof(oldVersion), 1, f) == 1);
    fclose(f);
    assert(!ReadSlotFromDisk(0));
    return 0;
}
