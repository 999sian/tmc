#include <stdio.h>
#include <string.h>
#include "enemy.h"
#include "definitions.h"
#include "projectile.h"
#include "port_config.h"
#include "ui.h"
#include "port_sprite_region.h"

RomRegion gRomRegion;
int gActiveRegion;
bool32 ProjectileInit(Entity*);
bool32 EnemyInit(Enemy*);
#define UI_STUB(name) void name(UIElement* element) { (void)element; }
UI_STUB(ButtonUIElement)
UI_STUB(ItemUIElement)
UI_STUB(TextUIElement)
UI_STUB(HeartUIElement)
UI_STUB(EzloNagUIElement)
#include "ui_initializer.inc"

const ProjectileDefinition gProjectileDefinitions[0x25] = {
    [CANNONBALL_PROJECTILE] = { .spriteIndex = SPRITE_CANNONBALLPROJECTILE },
    [V1_EYE_LASER] = { .spriteIndex = SPRITE_V1EYELASER },
    [V1_DARK_MAGIC_PROJECTILE] = { .spriteIndex = SPRITE_V1DARKMAGICPROJECTILE },
    [ARROW_PROJECTILE] = { .spriteIndex = SPRITE_ARROWPROJECTILE },
    [SPIKED_ROLLERS] = { .spriteIndex = SPRITE_SPIKEDROLLERS },
    [V2_PROJECTILE] = { .spriteIndex = SPRITE_V2PROJECTILE },
    [GYORG_MALE_ENERGY_PROJECTILE] = { .spriteIndex = SPRITE_GYORGMALEENERGYPROJECTILE },
    [BALL_AND_CHAIN] = { .spriteIndex = 489 },
    [BONE_PROJECTILE] = { .spriteIndex = SPRITE_BONEPROJECTILE },
};
const ProjectileDefinition gProjectileDefinition_12_alt[] = { { 0 } };
const ProjectileDefinition gProjectileDefinition_25_eu[] = { { .spriteIndex = 488 } };
const ProjectileDefinition gProjectileDefinition_14_eu[] = { { 0 } };
const ProjectileDefinition gProjectileDefinition_22_eu[] = { { 0 } };
EnemyDefinition gEnemyDefinitions[0x70] = {
    [VAATI_TRANSFIGURED_EYE] = { .spriteIndex = SPRITE_ENEMY5A },
    [GYORG_FEMALE_EYE] = { .spriteIndex = SPRITE_GYORGFEMALEEYE },
    [LEEVER] = { .spriteIndex = SPRITE_LEEVER },
};
EnemyDefinition gEnemyDefinitions_eu[0x70] = {
    [VAATI_TRANSFIGURED_EYE] = { .spriteIndex = SPRITE_ENEMY5A },
    [GYORG_FEMALE_EYE] = { .spriteIndex = SPRITE_GYORGFEMALEEYE },
    [LEEVER] = { .spriteIndex = SPRITE_LEEVER },
};
bool32 LoadFixedGFX(Entity* e, u32 n) { return TRUE; }
bool32 LoadSwapGFX(Entity* e, u32 n, u32 s) { return TRUE; }
u32 LoadObjPalette(Entity* e, u32 n) { return 0; }
void UpdateSpriteForCollisionLayer(Entity* e) {}
Entity* CreateObject(u32 id, u32 type, u32 type2) { return NULL; }
void CopyPosition(Entity* a, Entity* b) {}
static int failures;
static void check(unsigned got, unsigned expected, const char* label) {
    if (got != expected) {
        fprintf(stderr, "FAIL region%d %s got%u expected%u\n", gRomRegion, label, got, expected);
        failures++;
    }
}
int main(void) {
    const RomRegion regions[] = { ROM_REGION_USA, ROM_REGION_EU, ROM_REGION_JP };
    const unsigned projectileIds[] = { CANNONBALL_PROJECTILE, V1_EYE_LASER, V1_DARK_MAGIC_PROJECTILE,
                                       ARROW_PROJECTILE, BALL_AND_CHAIN, BONE_PROJECTILE,
                                       SPIKED_ROLLERS, V2_PROJECTILE, GYORG_MALE_ENERGY_PROJECTILE };
    const unsigned usaSprites[] = { 291, 292, 293, 321, 489, 264, 302, 316, 306 };
    const unsigned euSprites[] = { 290, 291, 292, 320, 488, 264, 301, 315, 305 };
    for (unsigned r = 0; r < 3; r++) {
        gRomRegion = regions[r];
        gActiveRegion = r == 1 ? TMC_REGION_EU : r == 2 ? TMC_REGION_JP : TMC_REGION_USA;
        check(Port_LogicalSpriteIndex(287), 287, "last index before EU omission");
        check(Port_LogicalSpriteIndex(288), r == 1 ? 0xffff : 288, "omitted EU sprite");
        Port_InitUIElementDefinitions();
        for (unsigned i = 0; i < 11; i++)
            check(gUIElementDefinitions[i].spriteIndex, (i < 3 ? 505 : 322) - (r == 1), "HUD");
        for (unsigned i = 0; i < sizeof(projectileIds) / sizeof(*projectileIds); i++) {
            Entity e = { 0 };
            e.id = projectileIds[i];
            check(ProjectileInit(&e), TRUE, "projectile init");
            check(e.spriteIndex, r == 1 ? euSprites[i] : usaSprites[i], "projectile native index");
            ProjectileInit(&e);
            check(e.spriteIndex, r == 1 ? euSprites[i] : usaSprites[i], "projectile init idempotent");
        }
        Enemy e = { 0 };
        e.base.id = VAATI_TRANSFIGURED_EYE;
        EnemyInit(&e);
        check(e.base.spriteIndex, r == 1 ? 296 : 297, "Vaati eye native index");
        EnemyInit(&e);
        check(e.base.spriteIndex, r == 1 ? 296 : 297, "enemy init idempotent");
        memset(&e, 0, sizeof(e));
        e.base.id = GYORG_FEMALE_EYE;
        EnemyInit(&e);
        check(e.base.spriteIndex, r == 1 ? 311 : 312, "Gyorg eye native index");
    }
    if (!failures) puts("regional sprite assignment boundaries: PASS (USA/EU/JP)");
    return failures != 0;
}
