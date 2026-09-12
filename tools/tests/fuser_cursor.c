/* Exercise the production scanner with a save-controlled cursor and ROM record. */
#include <stdio.h>
#include "src/common.c"

SaveFile gSave;
static u8 record[300];
static int failures;
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); failures++; } } while (0)
u32 GetFuserId(Entity* entity) { return 1; }
void* Port_GetFuserFusionData(u32 id) { return record; }
u32 GetInventoryValue(u32 item) { return 1; }
u32 Random(void) { return 0; }

static void Reset(void) {
    memset(&gSave, 0, sizeof(gSave));
    memset(record, 0, sizeof(record));
    record[1] = 100;
    record[5] = 0x29;
    record[6] = 0x25;
}
static void Reject(void) {
    SaveFile before = gSave;
    CHECK(GetFusionToOffer(NULL) == KINSTONE_NONE);
    CHECK(memcmp(&before, &gSave, sizeof(gSave)) == 0);
}
int main(void) {
    Reset();
    CHECK(GetFusionToOffer(NULL) == 0x29);
    CHECK(gSave.kinstones.fuserProgress[1] == 0);
    gSave.kinstones.fuserOffers[1] = KINSTONE_JUST_FUSED;
    CHECK(GetFusionToOffer(NULL) == 0x25);
    CHECK(gSave.kinstones.fuserProgress[1] == 1);
    gSave.kinstones.fuserOffers[1] = KINSTONE_JUST_FUSED;
    CHECK(GetFusionToOffer(NULL) == KINSTONE_NONE);
    CHECK(gSave.kinstones.fuserProgress[1] == 2);
    CHECK(gSave.kinstones.fuserOffers[1] == KINSTONE_FUSER_DONE);
    Reset(); gSave.kinstones.fuserProgress[1] = 255; record[260] = 0x40; Reject();
    Reset(); gSave.kinstones.fuserProgress[1] = 3; record[8] = 0x40; Reject();
    Reset(); gSave.kinstones.fuserProgress[1] = 2;
    gSave.kinstones.fuserOffers[1] = KINSTONE_JUST_FUSED; record[8] = 0x40; Reject();
    Reset(); gSave.kinstones.fuserProgress[1] = 2;
    gSave.kinstones.fuserOffers[1] = KINSTONE_RANDOM; Reject();
    Reset(); gSave.kinstones.fuserOffers[1] = 0x80; Reject();
    Reset(); memset(record + 5, 0x29, 7); Reject(); /* No bounded terminator. */
    Reset(); record[5] = 0x80; Reject(); /* Invalid ROM list entry. */
    Reset();
    for (int i = 0; i < 6; ++i) { record[5 + i] = i + 1; WriteBit(gSave.kinstones.fusedKinstones, i + 1); }
    record[11] = 0;
    CHECK(GetFusionToOffer(NULL) == KINSTONE_NONE);
    CHECK(gSave.kinstones.fuserProgress[1] == 6);
    Reset(); WriteBit(gSave.kinstones.fusedKinstones, 0x29);
    gSave.kinstones.fuserOffers[1] = KINSTONE_NEEDS_REPLACEMENT;
    CHECK(GetFusionToOffer(NULL) == 0x25);
    CHECK(gSave.kinstones.fuserProgress[1] == 1);
    Reset(); record[1] = 0; /* Retail fickleness still stores a valid pending offer. */
    CHECK(GetFusionToOffer(NULL) == KINSTONE_NONE);
    CHECK(gSave.kinstones.fuserOffers[1] == 0x29);
    Reset(); record[5] = KINSTONE_RANDOM; record[6] = 0x25;
    CHECK(GetFusionToOffer(NULL) >= 1 && gSave.kinstones.fuserProgress[1] == 0);
    Reset(); record[5] = KINSTONE_RANDOM;
    for (unsigned i = 0; i < sizeof(SharedFusions); ++i) WriteBit(gSave.kinstones.fusedKinstones, SharedFusions[i]);
    CHECK(GetFusionToOffer(NULL) == 0x25); /* Exhausted random pool advances safely. */
    CHECK(gSave.kinstones.fuserProgress[1] == 1);
    return failures != 0;
}
