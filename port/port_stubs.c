/**
 * @file port_stubs.c
 * @brief C implementations of functions originally in GBA assembly.
 *
 * These replace the auto-generated stubs in stubs_autogen.c.
 */

#include "global.h"
#include "script.h"

/*
 * Script command reading functions.
 * Original GBA assembly in asm/src/script.s reads 16-bit aligned data
 * from the script instruction pointer.
 *
 * Script instructions are packed as halfwords (u16):
 *   [0] = opcode: bits 0-9 = command index, bits 10-15 = command size (halfwords)
 *   [1..N] = arguments (halfwords, combined into words as needed)
 */

u32 GetNextScriptCommandHalfword(u16* ptr) {
    return ptr[0];
}

u32 GetNextScriptCommandHalfwordAfterCommandMetadata(u16* ptr) {
    return ptr[1];
}

u32 GetNextScriptCommandWord(u16* ptr) {
    return (u32)ptr[0] | ((u32)ptr[1] << 16);
}

u32 GetNextScriptCommandWordAfterCommandMetadata(u16* ptr) {
    return (u32)ptr[1] | ((u32)ptr[2] << 16);
}

/*
 * Subtask function pointer tables.
 * Originally in data/const/subtask.s as .4byte arrays.
 */

extern void Subtask_FadeIn(void);
extern void Subtask_Init(void);
extern void Subtask_Update(void);
extern void Subtask_FadeOut(void);
extern void Subtask_Die(void);

void (*const gUnk_0812901C[])(void) = {
    Subtask_FadeIn, Subtask_Init, Subtask_Update, Subtask_FadeOut, Subtask_Die,
};

extern void Subtask_Exit(void);
extern void Subtask_PauseMenu(void);
extern void Subtask_MapHint(void);
extern void Subtask_KinstoneMenu(void);
extern void Subtask_AuxCutscene(void);
extern void Subtask_PortalCutscene(void);
extern void Subtask_FigurineMenu(void);
extern void Subtask_WorldEvent(void);
extern void Subtask_FastTravel(void);
extern void Subtask_LocalMapHint(void);

void (*const gSubtasks[])(void) = {
    Subtask_Exit,         Subtask_PauseMenu,    Subtask_Exit, /* index 2 = same as Exit */
    Subtask_MapHint,      Subtask_KinstoneMenu, Subtask_AuxCutscene, Subtask_PortalCutscene,
    Subtask_FigurineMenu, Subtask_WorldEvent,   Subtask_FastTravel,  Subtask_LocalMapHint,
};
