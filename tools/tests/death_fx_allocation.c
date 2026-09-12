#include <stdio.h>
#include "src/enemyUtils.c"

SaveFile gSave;
RoomTransition gRoomTransition;
static DeathFxObject fx;
static int available, deleted, failures;
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); failures++; } } while (0)
Entity* CreateObject(u32 id, u32 type, u32 type2) { return available ? &fx.base : NULL; }
void EnemyDisableRespawn(Enemy* enemy) {}
void SetEntityPriority(Entity* entity, u32 priority) {}
void PositionRelative(Entity* origin, Entity* entity, s32 x, s32 y) {}
void CopyPosition(Entity* origin, Entity* entity) {}
void DeleteThisEntity(void) { deleted++; }
void DeleteEntity(Entity* entity) { deleted++; }
void sub_0807CD9C(void) {}
void SoundReq(u32 sound) {}

int main(void) {
    for (int success = 0; success <= 1; success++) {
        Enemy enemy = { 0 };
        available = success;
        deleted = 0;
        fx.unk6c = 0;
        enemy.enemyFlags = EM_FLAG_NO_DEATH_FX;
        EnemyCreateDeathFX(&enemy, 1, 0);
        CHECK(deleted == 1);
        CHECK(!success || (fx.unk6c & 8));
        enemy = (Enemy){ 0 };
        deleted = 0;
        enemy.base.contactFlags = 0x13;
        enemy.base.gustJarFlags = 1;
        EnemyCreateDeathFX(&enemy, 1, 0);
        CHECK(deleted == 1);
        CHECK(!success || ((fx.unk6c & 4) && fx.base.parent == NULL));
    }
    return failures != 0;
}
