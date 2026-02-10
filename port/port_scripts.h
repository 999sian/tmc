/**
 * @file port_scripts.h
 * @brief Script data resolution for the PC port.
 *
 * On GBA, script symbols (e.g., script_PlayerIntro) are labels in the ROM's
 * .text section. C code uses &script_name to get the script data address.
 *
 * On 64-bit PC, these symbols are u32 stubs in port_linked_stubs.c, and
 * &script_name yields a meaningless address. Instead, we resolve the GBA
 * ROM address of each script to a native pointer into the loaded ROM data.
 */
#pragma once

#ifdef PC_PORT

#include "port_rom.h"

/* Known GBA ROM addresses of script data (USA version).
 * These are the addresses of script labels in data/scripts.s. */
#define GBA_script_PlayerIntro 0x08009B30
#define GBA_script_PlayerWakeAfterRest 0x08009E58
#define GBA_script_PlayerWakingUpAtSimons 0x08011C50
#define GBA_script_PlayerWakingUpInHyruleCastle 0x08009E88
#define GBA_script_PlayerSleepingInn 0x08010A5C
#define GBA_script_BedInLinksRoom 0x08009ECC
#define GBA_script_BedAtSimons 0x08009EF0

/* GBA ROM addresses of cutscene scripts (used in cutscene.c EntityData tables) */
#define GBA_script_IntroCameraTarget 0x08009A50
#define GBA_script_ZeldaMoveToLinksHouse 0x08009A84
#define GBA_script_HouseDoorIntro 0x08009AF8
#define GBA_script_CutsceneOrchestratorIntro2 0x08009A34
#define GBA_script_CutsceneOrchestratorIntro 0x08009918
#define GBA_script_SmithIntro 0x08009950
#define GBA_script_ZeldaIntro 0x080099DC
#define GBA_script_ZeldaLeaveLinksHouse 0x08009D6C
#define GBA_script_CutsceneOrchestratorMinishVaati 0x080153EC
#define GBA_script_MinishEzlo 0x0801550C
#define GBA_script_CutsceneMiscObjectMinishCap 0x08015618
#define GBA_script_Vaati 0x08015684
#define GBA_script_CutsceneOrchestratorTakeoverCutscene 0x08015CD4
#define GBA_script_KingDaltusTakeover 0x08015DF0
#define GBA_script_VaatiTakeover 0x08015E58
#define GBA_script_ZeldaStoneTakeover 0x08015FA4
#define GBA_script_MinisterPothoTakeover 0x08015F08
#define GBA_script_GuardTakeover 0x08015F3C
#define GBA_script_ZeldaStoneInDHC 0x0800DB18
#define GBA_script_ZeldaStoneDHC 0x0800E58C

/**
 * Resolve a script's GBA ROM address to a native PC pointer.
 * Usage: PORT_SCRIPT(script_PlayerIntro) instead of &script_PlayerIntro
 */
#define PORT_SCRIPT(name) ((void*)Port_ResolveRomData(GBA_##name))

/**
 * Store a script's GBA ROM address as a u32 constant for EntityData tables.
 * On PC, we store the GBA address; at runtime sub_0804AF0C resolves it.
 * Usage: ENTITY_SCRIPT(script_Foo) instead of (u32)&script_Foo
 */
#define ENTITY_SCRIPT(name) GBA_##name

#else /* !PC_PORT */

/* On GBA, just take the address of the symbol as usual. */
#define PORT_SCRIPT(name) (&name)
#define ENTITY_SCRIPT(name) (u32) & name

#endif /* PC_PORT */
