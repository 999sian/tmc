#include <stdio.h>
#include "src/object/chestSpawner.c"

RoomControls gRoomControls;
static TileEntity tiles[] = { { .type = BIG_CHEST, .localFlag = 0x76, ._2 = ITEM_BOTTLE1 }, { 0 } };
static int committed, attempted, available, interactable, failures;
static u16 pendingFlag;
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); failures++; } } while (0)
u32 sub_0800445C(Entity* entity) { return 0; }
void GetNextFrame(Entity* entity) {}
void SetLocalFlag(u32 flag) { committed++; }
void* GetCurrentRoomProperty(u32 property) { return tiles; }
bool Rando_OverrideLocationKey(u32 key, u8* item, u8* subtype) { return false; }
bool32 CreateItemEntityWithFlag(u32 item, u32 subtype, u32 delay, u16 flag) {
    attempted++;
    if (!available) return FALSE;
    pendingFlag = flag;
    return TRUE;
}
void CreateItemEntity(u32 item, u32 subtype, u32 delay) { CreateItemEntityWithFlag(item, subtype, delay, 0); }
void AddInteractableChest(ChestSpawnerEntity* chest) { interactable++; }
void SetMultipleTiles(const TileData* data, u32 position, u32 layer) {}
void InitializeAnimation(Entity* entity, u32 animation) {}
void SoundReq(u32 sound) {}

int main(void) {
    ChestSpawnerEntity chest = { 0 };
    chest.base.action = 4;
    chest.base.type2 = 0x76;
    chest.base.frame = ANIM_DONE;
    chest.base.subtimer = 2;
    ChestSpawner_Type2Action4(&chest);
    CHECK(committed == 0 && attempted == 0); /* Opening delay cannot consume a reward. */
    ChestSpawner_Type2Action4(&chest);
    CHECK(attempted == 1 && committed == 0 && chest.base.action == 3 && interactable == 1);
    available = 1;
    chest.base.action = 4;
    chest.base.subtimer = 1;
    ChestSpawner_Type2Action4(&chest);
    CHECK(attempted == 2 && committed == 0 && pendingFlag == 0x76 && chest.base.action == 5);
    /* Missing room reward data must also leave a retryable chest. */
    tiles[0].type = 0;
    chest.base.action = 4;
    chest.base.subtimer = 1;
    ChestSpawner_Type2Action4(&chest);
    CHECK(committed == 0 && chest.base.action == 3 && interactable == 2);
    /* The rupee fountain has no item-get pair and still owns its completion bit. */
    chest.base.action = 4;
    chest.base.timer = 24;
    chest.base.subtimer = 1;
    ChestSpawner_Type2Action4(&chest);
    CHECK(committed == 1 && chest.base.action == 6 && attempted == 2);
    return failures != 0;
}
