#include "port_second_screen_state.h"

#include <string.h>

#ifdef __ANDROID__

#include <pthread.h>

#include "player.h"
#include "room.h"
#include "save.h"

static SecondScreenSnapshot sSnapshot;
static pthread_mutex_t sSnapshotMutex = PTHREAD_MUTEX_INITIALIZER;

void Port_SecondScreenState_Publish(void) {
    /* Assembled outside the lock: this runs on the game thread itself, the
     * same thread that owns gRoomControls/gPlayerEntity/gSave during normal
     * gameplay, so reading them here is exactly as safe as any other engine
     * code doing so — no cross-thread race on this side. The lock only
     * needs to guard the swap into sSnapshot, which the second-screen
     * thread does read cross-thread. */
    SecondScreenSnapshot next;
    next.area = gRoomControls.area;
    next.room = gRoomControls.room;
    next.playerX = gPlayerEntity.base.x.HALF.HI;
    next.playerY = gPlayerEntity.base.y.HALF.HI;
    next.equippedA = gSave.stats.equipped[SLOT_A];
    next.equippedB = gSave.stats.equipped[SLOT_B];

    pthread_mutex_lock(&sSnapshotMutex);
    sSnapshot = next;
    pthread_mutex_unlock(&sSnapshotMutex);
}

void Port_SecondScreenState_Read(SecondScreenSnapshot* out) {
    pthread_mutex_lock(&sSnapshotMutex);
    *out = sSnapshot;
    pthread_mutex_unlock(&sSnapshotMutex);
}

#else /* !__ANDROID__ — no second-screen thread to publish to. */

void Port_SecondScreenState_Publish(void) {}

void Port_SecondScreenState_Read(SecondScreenSnapshot* out) {
    memset(out, 0, sizeof(*out));
}

#endif
