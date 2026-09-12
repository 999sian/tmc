/* Regression inspired by EstebanPdN/zelda-tmc-3ds commit
 * 3b05a85eeba2f9fd053ec7ceb875040d3d25db9b. Exercise real init + dispatch. */
#include <stdio.h>
#include <string.h>
#include "src/npcUtils.c"
#include "src/npc.c"

int gActiveRegion = TMC_REGION_USA;
RoomControls gRoomControls;
NPCStruct gNPCData[NPC_DATA_CAPACITY];
u8 gUnk_020342F8[4];
static int available, palettes, updates, draws, failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); failures++; } } while (0)
#define NPC_DEF(mode) { .bitfield = { .type = 1, .hitbox = 3, .gfx = 1, .gfx_type = mode }, \
    .data.sprite = { .paletteIndex = 191, .spriteIndex = SPRITE_GORON, .spritePriority = 1, .draw = 1 } }
const NPCDefinition gNPCDefinitions[128] = { [GORON] = NPC_DEF(1), [FOREST_MINISH] = NPC_DEF(0) };
const NPCDefinition gNPCDefinitions_eu[128] = { [GORON] = NPC_DEF(1), [FOREST_MINISH] = NPC_DEF(0) };
const Hitbox gHitbox_2 = { 0 }, gHitbox_3 = { 0 }, gHitbox_30 = { 0 }, gHitbox_31 = { 0 };
bool32 LoadSwapGFX(Entity* e, u32 count, u32 slot) { return available; }
bool32 LoadFixedGFX(Entity* e, u32 gfx) { return available; }
u32 LoadObjPalette(Entity* e, u32 palette) { palettes++; return 0; }
void UpdateSpriteForCollisionLayer(Entity* e) {}
u32 ReadBit(void* data, u32 bit) { return 1; }
void DeleteThisEntity(void) {}
bool32 EntityDisabled(Entity* e) { return FALSE; }
void DrawEntity(Entity* e) { draws++; }
static void UpdateNPC(Entity* e) { updates++; e->action = 1; }
void (*const gNPCFunctions[128][3])(Entity*) = { [GORON] = { UpdateNPC }, [FOREST_MINISH] = { UpdateNPC } };

int main(void) {
    const u8 ids[] = { GORON, FOREST_MINISH };
    for (int region = TMC_REGION_USA; region <= TMC_REGION_JP; region++) {
        gActiveRegion = region;
        for (unsigned i = 0; i < sizeof(ids); i++) {
            Entity npc = { 0 };
            npc.id = ids[i];
            npc.next = &npc;
            npc.spriteIndex = 0x1ff;
            npc.animIndex = 0x44;
            available = palettes = updates = draws = 0;
            NPCUpdate(&npc);
            CHECK(!(npc.flags & ENT_DID_INIT));
            CHECK(npc.action == 0 && npc.spriteIndex == 0x1ff && npc.animIndex == 0x44);
            CHECK(palettes == 0 && updates == 0 && draws == 0);
            available = 1;
            NPCUpdate(&npc);
            CHECK(npc.flags & ENT_DID_INIT);
            CHECK(npc.spriteIndex == SPRITE_GORON && npc.animIndex == 0xff);
            CHECK(palettes == 1 && updates == 1 && draws == 1);
            NPCUpdate(&npc);
            CHECK(palettes == 1 && updates == 2 && draws == 2);
        }
    }
    return failures ? 1 : 0;
}
