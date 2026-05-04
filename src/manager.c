#include "manager.h"
#include "room.h"
#ifdef PC_PORT
#include <stdio.h>
#endif

// TODO: change all manager arguments to be Entity* and cast to specific type later.
void (*const gMiscManagerunctions[])() = {
    NULL,
    LightRayManager_Main,
    VerticalMinishPathBackgroundManager_Main,
    MinishPortalManager_Main,
    DiggingCaveEntranceManager_Main,
    BridgeManager_Main,
    SpecialWarpManager_Main,
    MinishVillageTileSetManager_Main,
    HorizontalMinishPathBackgroundManager_Main,
    MinishRaftersBackgroundManager_Main,
    EzloHintManager_Main,
    FightManager_Main,
    RollingBarrelManager_Main,
    TileChangeObserveManager_Main,
    EntitySpawnManager_Main,
    MiscManager_Main,
    WeatherChangeManager_Main,
    FlagAndOperatorManager_Main,
    HyruleTownTileSetManager_Main,
    HouseSignManager_Main,
    SteamOverlayManager_Main,
    TempleOfDropletsManager_Main,
    DelayedEntityLoadManager_Main,
    FallingItemManager_Main,
    CloudOverlayManager_Main,
    PowBackgroundManager_Main,
    HoleManager_Main,
    StaticBackgroundManager_Main,
    RainfallManager_Main,
    AnimatedBackgroundManager_Main,
    RegionTriggerManager_Main,
    RailIntersectionManager_Main,
    MoveableObjectManager_Main,
    MinishSizedEntranceManager_Main,
    LightManager_Main,
    LightLevelSetManager_Main,
    BombableWallManager_Main,
    FlameManager_Main,
    PushableFurnitureManager_Main,
    ArmosInteriorManager_Main,
    EnemyInteractionManager_Main,
    Manager29_Main,
    DestructibleTileObserveManager_Main,
    AngryStatueManager_Main,
    CloudStaircaseTransitionManager_Main,
    WaterfallBottomManager_Main,
    SecretManager_Main,
    Vaati3BackgroundManager_Main,
    TilePuzzleManager_Main,
    GoronMerchantShopManager_Main,
    VaatiAppearingManager_Main,
    HyruleTownBellManager_Main,
    Vaati3InsideArmManager_Main,
    CameraTargetManager_Main,
    RepeatedSoundManager_Main,
    Vaati3StartManager_Main,
    FloatingPlatformManager_Main,
    EnterRoomTextboxManager_Main,
};

void ManagerUpdate(Entity* this) {
    if (EntityDisabled(this)) {
        return;
    }

    if (this->id >= ARRAY_COUNT(gMiscManagerunctions) || gMiscManagerunctions[this->id] == NULL) {
#ifdef PC_PORT
        fprintf(stderr, "[MANAGER] invalid id=%u ptr=%p type=%u type2=%u action=%u\n", this->id, (void*)this,
                this->type, this->type2, this->action);
#endif
        return;
    }

#ifdef PC_PORT
    if (this->id == ENTER_ROOM_TEXTBOX_MANAGER) {
        fprintf(stderr, "[MANAGER] dispatch ENTER_ROOM_TEXTBOX_MANAGER ptr=%p type=%u type2=%u action=%u room=%u area=%u\n",
                (void*)this, this->type, this->type2, this->action, gRoomControls.room, gRoomControls.area);
    }
#endif
    gMiscManagerunctions[this->id](this);
}
