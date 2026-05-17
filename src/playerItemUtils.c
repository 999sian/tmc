#include "common.h"
#include "entity.h"
#include "flags.h"
#include "item.h"
#include "message.h"
#include "object.h"
#include "room.h"
#include "player.h"
#include "save.h"
#include "sound.h"

static Entity* GiveItemWithCutscene(u32, u32, u32);
static void InitTileMessage(u32, u32);

void SetPlayerItemGetState(Entity*, u32, u32);

void CreateItemEntity(u32 type, u32 type2, u32 delay) {
    Entity* e = GiveItemWithCutscene(type, type2, delay);
    if (e != NULL) {
        e->parent = CreateLinkAnimation(e, e->type, 0);
    }
}

void InitItemGetSequence(u32 type, u32 type2, u32 delay) {
    Entity* e = GiveItemWithCutscene(type, type2, delay);
    if (e != NULL) {
        e->parent = &gPlayerEntity.base;
        SetPlayerItemGetState(e, e->type, 0);
    }
}

#ifdef PC_PORT
#include <stdbool.h>
/* Generic randomizer hook — called at the centralized point where any
 * item entity gets spawned (chests, NPC gifts, drops, cutscenes...).
 * Substitutes *item if the active seed's permutation remaps it. */
extern bool Rando_OverrideItem(u8* type, u8* subtype);
#endif

static Entity* GiveItemWithCutscene(u32 item, u32 type2, u32 delay) {
    Entity* e;
    if (item == ITEM_SHELLS && gSave.stats.hasAllFigurines) {
        item = ITEM_RUPEE50;
        type2 = 0;
    }
#ifdef PC_PORT
    {
        u8 t = (u8)item, s = (u8)type2;
        if (Rando_OverrideItem(&t, &s)) {
            item  = t;
            type2 = s;
        }
    }
#endif
    e = CreateAuxPlayerEntity();
    if (e != NULL) {
        e->type = item;
        e->type2 = type2;
        e->timer = delay;
        e->id = LINK_HOLDING_ITEM;
        e->kind = OBJECT;
        AppendEntityToList(e, 6);
    }
    return e;
}

void ClearSmallChests(void) {
    MemClear(gSmallChests, sizeof(gSmallChests));
}

void OpenSmallChest(u32 pos, u32 layer) {
    TileEntity* t = gSmallChests;
    u32 found = 0;
    u32 i;
    for (i = 0; i < 8; ++i, ++t) {
        if (*(u16*)&t->tilePos == pos) {
            found = 1;
            break;
        }
    }
    if ((layer >> 1) == ((u32)(t->_6 << 31) >> 31)) {
        if (found) {
            SetLocalFlag(t->localFlag);
            /* port/rando interception now happens centrally inside
             * GiveItemWithCutscene — see the Rando_OverrideItem hook
             * up the call stack. No per-source hook needed here. */
            CreateItemEntity(t->_2, t->_3, 0);
        } else {
            CreateItemEntity(ITEM_FAIRY, 0, 0);
        }
        sub_0807B7D8(0x74, pos, layer);
        RequestPriorityDuration(NULL, 120);
        SoundReq(SFX_CHEST_OPEN);
    }
}

u32 sub_080A7CFC(u32 a1) {
    u32 msg = TEXT_INDEX(TEXT_LOCATIONS, 0x0);
    bool32 hint = FALSE;

    TileEntity* t = GetCurrentRoomProperty(3);
    if (t != 0) {
        do {
            if (t->tilePos == a1) {
                switch (t->type) {
                    case SIGN:
                        hint = FALSE;
                        msg = *(u16*)&t->_6;
                        break;
                    case TILE_EZLO_HINT:
                        hint = TRUE;
                        msg = *(u16*)&t->_6;
                        break;
                }
                break;
            }
            t++;
        } while (t->tilePos != 0);
    }
    InitTileMessage(msg, hint);
}

static void InitTileMessage(u32 msg, u32 hint) {
    if (hint) {
        CreateEzloHint(msg, 0);
    } else {
        // Read sign text
        MessageFromTarget(msg);
    }
}
