
#include "area.h"
#include "backgroundAnimations.h"
#include "beanstalkSubtask.h"
#include "collision.h"
#include "color.h"
#include "common.h"
#include "entity.h"
#include "fade.h"
#include "fileselect.h"
#include "hitbox.h"
#include "kinstone.h"
#include "main.h"
#include "map.h"
#include "menu.h"
#include "message.h"
#include "npc.h"
#include "player.h"
#include "room.h"
#include "save.h"
#include "screen.h"
#include "script.h"
#include "sound.h"
#include "structures.h"

uint16_t gPortIntrCheck;
void* gPortIntrVector;
struct SoundInfo* gPortSoundInfoPtr;

// Data globals
GfxSlotList gGFXSlots;
Message gMessage;
TextRender gTextRender;
u16 gBG0Buffer[0x400];
u16 gPaletteBuffer[0x200];
Input gInput;
u32 gRand;
Screen gScreen;
OAMCommand gOamCmd;
Main gMain;
FadeControl gFadeControl;
OAMControls gOAMControls;
SoundPlayingInfo gSoundPlayingInfo;

u32 gUsedPalettes;

// Pointers
struct_02000010 gUnk_02000010;
u32 gUnk_02000030 = 0x02000030;
struct_02000040 gUnk_02000040;
u32 gUnk_020000B0 = 0x020000B0;
struct_gUnk_020000C0 gUnk_020000C0[0x30];
Palette gUnk_02001A3C;
u32 gUnk_02006F00 = 0x02006F00;
u32 gUnk_0200B640 = 0x0200B640;
u32 gUnk_02017830 = 0x02017830;
u32 gUnk_02017AA0 = 0x02017AA0;
u32 gUnk_02017BA0 = 0x02017BA0;
u32 gUnk_02018EA0 = 0x02018EA0;
struct_02018EB0 gUnk_02018EB0;
u32 gUnk_02018EE0 = 0x02018EE0;
u32 gUnk_02021F00 = 0x02021F00;
u32 gUnk_020227DC = 0x020227DC;
struct_020227E8 gUnk_020227E8[16];
u32 gUnk_020227F0 = 0x020227F0;
u32 gUnk_020227F8 = 0x020227F8;
u32 gUnk_02022800 = 0x02022800;
u32 gUnk_02022830 = 0x02022830;
u8 gUnk_02024048 = 0; /* pending sound count (used by DrawEntity) */
u32 gUnk_020246B0 = 0x020246B0;
u32 gUnk_02033290 = 0x02033290;
u32 gUnk_020342F8 = 0x020342F8;
u32 gUnk_02034330 = 0x02034330;
struct_02034480 gUnk_02034480;
u32 gUnk_02034492 = 0x02034492;
u32 gUnk_020344A0 = 0x020344A0;
struct_020354C0 gUnk_020354C0[32];
u32 gUnk_02035542 = 0x02035542;
u32 gUnk_02036540 = 0x02036540;
u32 gUnk_02036A58 = 0x02036A58;
u32 gUnk_02036AD8 = 0x02036AD8;
u32 gUnk_02036BB8 = 0x02036BB8;
// ========== Global data (converted from function stubs) ==========

// Core systems
UI gUI;
HUD gHUD;
Menu gMenu;
Area gArea;
SaveFile gSave;
VBlankDMA gVBlankDMA;
u8 gUpdateVisibleTiles;

// Player
PlayerEntity gPlayerEntity;
PlayerState gPlayerState;
Entity* gPlayerClones[3];
ScriptExecutionContext gPlayerScriptExecutionContext;

// Entities
GenericEntity gEntities[MAX_ENTITIES];
GenericEntity gAuxPlayerEntities[MAX_AUX_PLAYER_ENTITIES];
LinkedList gEntityLists[9];
LinkedList gEntityListsBackup;
u8 gEntCount;
u8 gManagerCount;
CarriedEntity gCarriedEntity;
ItemBehavior gActiveItems[MAX_ACTIVE_ITEMS];
PriorityHandler gPriorityHandler;
PossibleInteraction gPossibleInteraction;

// Room / Map
RoomControls gRoomControls;
MapLayer gMapBottom;
MapLayer gMapTop;
RoomTransition gRoomTransition;
RoomVars gRoomVars;
RoomMemory gRoomMemory[4]; // sized to 4 entries (typical for GBA)
RoomMemory* gCurrentRoomMemory;
void** gCurrentRoomProperties;

// BG Buffers
u16 gBG1Buffer[0x400];
u16 gBG2Buffer[0x400];
u16 gBG3Buffer[0x800];

// Script
ActiveScriptInfo gActiveScriptInfo;
ScriptExecutionContext gScriptExecutionContextArray[0x20];

// Misc
BgAnimation gBgAnimations[MAX_BG_ANIMATIONS];
ChooseFileState gChooseFileState;
u8 gTextGfxBuffer[0xD00];
u8 gPaletteBufferBackup[0x400]; // BG+OBJ palettes backup
u8 gCollidableCount;

// gIntroState aliases gMenu (they share the same memory region on GBA)
// IntroState is a file-local type in title.c -- we provide a byte alias here
u8 gIntroState[0x40] __attribute__((aligned(4)));

// gFrameObjLists — sprite frame data loaded from ROM (200KB)
// Uses self-relative u32 offsets internally.
u32 gFrameObjLists[50016];

// gMapData — generic map data buffer
u8 gMapData[0x4000];

// gSubtasks — see subtask.c for actual pointer table; stub as NULL-terminated
// (This is actually const data from ROM, not a writable variable.
//  It will be populated properly once subtask data is loaded.)

// gCollisionMtx — collision matrix
u8 gCollisionMtx[173 * 34]; // ColSettings is file-local, using u8

// gUpdateContext — file-local type in entity.c; placeholder buffer
u8 gUpdateContext[64] __attribute__((aligned(4)));

// gInteractableObjects — file-local type; placeholder buffer
u8 gInteractableObjects[0x200] __attribute__((aligned(4)));

// === Additional data stubs (converted from function stubs) ===

// Color / Palette
Palette gPaletteList[0x10];

// Hitbox data — these are const ROM data; zero-init is safe (never matched on title screen)
const Hitbox gHitbox_0, gHitbox_1, gHitbox_2, gHitbox_3, gHitbox_4, gHitbox_5;
const Hitbox gHitbox_6, gHitbox_7, gHitbox_8, gHitbox_9, gHitbox_10, gHitbox_11;
const Hitbox gHitbox_12, gHitbox_13, gHitbox_14, gHitbox_15, gHitbox_16, gHitbox_17;
const Hitbox gHitbox_18, gHitbox_20, gHitbox_21, gHitbox_22, gHitbox_23, gHitbox_24;
const Hitbox gHitbox_27, gHitbox_29, gHitbox_30, gHitbox_31, gHitbox_32;
const Hitbox3D gHitbox_19, gHitbox_25, gHitbox_26, gHitbox_28;

// NPC data
NPCStruct gNPCData[50];

// Pause menu
PauseMenuOptions gPauseMenuOptions;

// Sprite data (pointer table — will be loaded from ROM later)
SpritePtr gSpritePtrs[512]; // sized generously

// Map data — NOTE: gMapDataBottomSpecial is declared differently in
// tileMap.h (u16[0x4000]) vs fileselect.h (struct_02019EE0). We use
// the struct_02019EE0 type since fileselect.h is included.
struct_02019EE0 gMapDataBottomSpecial;
// gMapDataTopSpecial — same size (0x8000 bytes)
u8 gMapDataTopSpecial[0x8000] __attribute__((aligned(4)));
u32 gDungeonMap[0x800];

// Heap
u8 gzHeap[0x1000];

// Kinstone fusion
FuseInfo gFuseInfo;

// Room / tile entities
TileEntity gSmallChests[8];
SpecialTileEntry gTilesForSpecialTiles[MAX_SPECIAL_TILES];

// Collision — LinkedList2 is file-local in collision.c (5 fields, ~20 bytes each)
u8 gUnk_03003C70[16 * 20] __attribute__((aligned(4)));

// IWRAM scratch
u8 gUnk_03000420[0x800] __attribute__((aligned(4)));
u8 gUnk_03000C30;
u8 gUnk_03001020[sizeof(Screen)] __attribute__((aligned(4)));

// Sound player data — MusicPlayerInfo/Track types from m4a
// These are complex structs; we provide byte buffers matching expected sizes
u8 gMPlayInfos[0x1C * 0x50] __attribute__((aligned(4))); // MusicPlayerInfo is ~0x50 bytes each
u8 gMPlayInfos2[0x4 * 0x50] __attribute__((aligned(4)));
u8 gMPlayTracks[0x50 * 16] __attribute__((aligned(4))); // MusicPlayerTrack ~0x50 bytes each

// BGM song headers — ROM data, zero-init stubs
// These would normally come from the sound data in ROM.
u8 bgmBeanstalk[0x10], bgmBeatVaati[0x10], bgmBossTheme[0x10], bgmCastleCollapse[0x10];
u8 bgmCastleMotif[0x10], bgmCastleTournament[0x10], bgmCastorWilds[0x10], bgmCaveOfFlames[0x10];
u8 bgmCloudTops[0x10], bgmCredits[0x10], bgmCrenelStorm[0x10], bgmCuccoMinigame[0x10];
u8 bgmDarkHyruleCastle[0x10], bgmDeepwoodShrine[0x10], bgmDiggingCave[0x10], bgmDungeon[0x10];
u8 bgmElementGet[0x10], bgmElementTheme[0x10], bgmElementalSanctuary[0x10], bgmEzloGet[0x10];
u8 bgmEzloStory[0x10], bgmEzloTheme[0x10], bgmFairyFountain[0x10], bgmFairyFountain2[0x10];
u8 bgmFestivalApproach[0x10], bgmFightTheme[0x10], bgmFightTheme2[0x10], bgmFileSelect[0x10];
u8 bgmFortressOfWinds[0x10], bgmGameover[0x10], bgmHouse[0x10], bgmHyruleCastle[0x10];
u8 bgmHyruleCastleNointro[0x10], bgmHyruleField[0x10], bgmHyruleTown[0x10], bgmIntroCutscene[0x10];
u8 bgmLearnScroll[0x10], bgmLostWoods[0x10], bgmLttpTitle[0x10], bgmMinishCap[0x10];
u8 bgmMinishVillage[0x10], bgmMinishWoods[0x10], bgmMtCrenel[0x10], bgmPalaceOfWinds[0x10];
u8 bgmPicoriFestival[0x10], bgmRoyalCrypt[0x10], bgmRoyalValley[0x10], bgmSavingZelda[0x10];
u8 bgmSecretCastleEntrance[0x10], bgmStory[0x10], bgmSwiftbladeDojo[0x10], bgmSyrupTheme[0x10];
u8 bgmTempleOfDroplets[0x10], bgmTitleScreen[0x10], bgmUnused[0x10], bgmVaatiMotif[0x10];
u8 bgmVaatiReborn[0x10], bgmVaatiTheme[0x10], bgmVaatiTransfigured[0x10], bgmVaatiWrath[0x10];
u8 bgmWindRuins[0x10];

// Linker symbols (RAM function boundaries — unused on PC)
u8 RAMFUNCS_END[4];
u8 gCopyToEndOfEwram_End[4];
u8 gCopyToEndOfEwram_Start[4];
u8 gEndOfEwram[4];
u8 sub_080B197C[4];
u8 ram_sub_080B197C[4];
u32 ram_MakeFadeBuff256;

// Area / room data — these are ROM pointer tables, zero-init for now
const AreaHeader gAreaMetadata[256];
RoomHeader* gAreaRoomHeaders[256];
void* gAreaRoomMaps[256];
void* gAreaTable[256];
void* gAreaTileSets[256];
void* gAreaTiles[256];

// Function pointer tables — zero-init means they'll be NULL (safe as long as not called)
void* gSubtasks[64];
void* ButtonUIElement_Actions[16];
void* EzloNagUIElement_Actions[16];
void* HoleManager_Actions[16];
u8 gUIElementDefinitions[256] __attribute__((aligned(4)));
void* Subtask_FastTravel_Functions[16];
void* Subtask_MapHint_Functions[16];

// Exit lists / transitions
const Transition* const* const gExitLists[256];
const Transition gExitList_RoyalValley_ForestMaze[4];

// Various game data
u32 gFixedTypeGfxData[256];
void* gCaveBorderMapData[16];
u8 gMessageChoices[32] __attribute__((aligned(4)));
u8 gOverworldLocations[256] __attribute__((aligned(4)));
void* gMoreSpritePtrs[64];
u8 gExtraFrameOffsets[256];
u8 gShakeOffsets[256];
u16 gDungeonNames[64];
u8 gFigurines[512] __attribute__((aligned(4)));
void* gLilypadRails[32];
u8 gMapActTileToSurfaceType[256] __attribute__((aligned(4)));
u8 gPalette_549[32];
void* gTranslations[16];
void* gWallMasterScreenTransitions[16];
void* gZeldaFollowerText[8];
void* gSpriteAnimations_322[64];
u32 gSpriteAnimations_GhostBrothers[64];
u8 gDiggingCaveEntranceTransition[32] __attribute__((aligned(4)));
u8 RupeeKeyDigits[16];

// Player macros
u8 gPlayerMacroBladeBrothers0[16], gPlayerMacroBladeBrothers1[16];
u8 gPlayerMacroBladeBrothers2[16], gPlayerMacroBladeBrothers3[16];
u8 gPlayerMacroBladeBrothers4[16], gPlayerMacroBladeBrothers5[16];
u8 gPlayerMacroBladeBrothers6[16], gPlayerMacroBladeBrothers7[16];

// Entity data (ROM data — all zero-init stubs)
u8 Entities_HouseInteriors1_Mayor_080D6210[64];
u8 Entities_MinishPaths_MayorsCabin_gUnk_080D6138[64];
u8 UpperInn_Din[64], UpperInn_Farore[64], UpperInn_Nayru[64];
u8 UpperInn_NoDin[64], UpperInn_NoFarore[64], UpperInn_NoNayru[64], UpperInn_Oracles[64];
u8 gUnk_additional_8_DeepwoodShrine_StairsToB1[64];
u8 gUnk_additional_8_HouseInteriors1_Library1F[64];
u8 gUnk_additional_8_HouseInteriors3_BorlovEntrance[64];
u8 gUnk_additional_8_HyruleCastle_3[64];
u8 gUnk_additional_8_MelarisMine_Main[64];
u8 gUnk_additional_8_PalaceOfWinds_GyorgTornado[64];
u8 gUnk_additional_9_HouseInteriors1_Library1F[64];
u8 gUnk_additional_9_HouseInteriors2_Percy[64];
u8 gUnk_additional_9_HouseInteriors3_BorlovEntrance[64];
u8 gUnk_additional_9_MelarisMine_Main[64];
u8 gUnk_additional_9_PalaceOfWinds_GyorgTornado[64];
u8 gUnk_additional_a_CaveOfFlamesBoss_Main[64];
u8 gUnk_additional_a_DeepwoodShrineBoss_Main[64];
u8 gUnk_additional_a_HouseInteriors2_Percy[64];
u8 gUnk_additional_a_HouseInteriors3_BorlovEntrance[64];
u8 gUnk_additional_a_TempleOfDroplets_BigOcto[64];
u8 gUnk_additional_c_HouseInteriors2_Romio[64];
u32 Enemies_LakeHylia_Main;
u32 Area_HyruleTown[16];

// Script data (ROM data — zero-init stubs so linker resolves them)
u16 script_08012C48;
u16 script_08015B14;
u16 script_BedAtSimons;
u16 script_BedInLinksRoom;
u8 script_BombMinish[32], script_BombMinishKinstone[32];
u16 script_BusinessScrubIntro[16];
u16 script_CutsceneMiscObjectSwordInChest;
u16 script_CutsceneMiscObjectTheLittleHat;
u16 script_EzloTalkOcarina[16];
u8 script_ForestMinish1[32], script_ForestMinish2[32], script_ForestMinish3[32];
u8 script_ForestMinish4[32], script_ForestMinish5[32], script_ForestMinish6[32];
u8 script_ForestMinish7[32], script_ForestMinish8[32], script_ForestMinish9[32];
u8 script_ForestMinish10[32], script_ForestMinish11[32], script_ForestMinish12[32];
u8 script_ForestMinish13[32], script_ForestMinish14[32], script_ForestMinish15[32];
u8 script_ForestMinish16[32], script_ForestMinish17[32], script_ForestMinish18[32];
u8 script_ForestMinish19[32], script_ForestMinish20[32], script_ForestMinish21[32];
u16 script_MazaalBossObjectMazaal[16];
u16 script_MazaalMacroDefeated[16];
u8 script_MinishVillageObjectLeftStoneOpening[4];
u8 script_MinishVillageObjectRightStoneOpening[4];
u16 script_PlayerAtDarkNut1[16], script_PlayerAtDarkNut2[16], script_PlayerAtDarkNut3[16];
u16 script_PlayerAtMadderpillar[16];
u16 script_PlayerGetElement[16];
u32 script_PlayerIntro;
void* script_PlayerSleepingInn[8];
u32 script_PlayerWakeAfterRest;
u32 script_PlayerWakingUpAtSimons;
u32 script_PlayerWakingUpInHyruleCastle;
u8 script_Rem[4];
u16 script_Stockwell;
u16 script_StockwellBuy[16];
u16 script_StockwellDogFood[16];
u8 script_TalonGotKey;
u16 script_WindTribespeople6;
u16 script_ZeldaMagic;

// Unk_08133368 — already defined in data_stubs_autogen.c