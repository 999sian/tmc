#ifndef FILESELECT_H
#define FILESELECT_H

#include "global.h"
#include "save.h"
#include "message.h"

typedef struct {
    /*0x00*/ u8 unk_0x0;
    /*0x02*/ u8 filler0[0x4];
    /*0x06*/ u8 state;
    /*0x07*/ u8 subState;
    /*0x08*/ u16 timer;
    /*0x0a*/ u8 fillerA[0x5];
    /*0x10*/ s16 unk_0x10;
    /*0x12*/ u16 unk_0x12;
    /*0x14*/ u8 fillerB[0xC];
    /*0x20*/ s8 unk_0x20;
    /*0x21*/ u8 fillerC[0xF];
} ChooseFileState;
static_assert(sizeof(ChooseFileState) == 0x30);

// TODO: This occupies the same memory region as gMenu
extern ChooseFileState gChooseFileState;

typedef struct {
    /*0x00*/ u8 isTransitioning;
    /*0x01*/ u8 unk1;
    /*0x02*/ u8 unk2;
    /*0x03*/ u8 unk3;
    /*0x04*/ u8 unk4;
    /*0x05*/ u8 unk5;
    /*0x06*/ u8 unk6;
    /*0x07*/ u8 unk7;
    /*0x08*/ s8 saveStatus[3];
    /*0x0b*/ SaveFile saves[3];
} struct_02019EE0;
extern struct_02019EE0 gMapDataBottomSpecial;
// TODO size: 0x8000 from ClearTileMaps?

extern u32 ShowTextBox(u32 textIndexOrPtr, const Font* font);
extern void ClearTileMaps(void);
extern void ResetSaveFile(u32);
extern void sub_08050A64(u32);
extern void sub_08050AFC(u32);
extern void sub_08050384();
extern void CreateDialogBox(u32, u32);

extern const Font gUnk_080FC844;
extern const u16 gUnk_080FC85C[][3];
extern void (*const gUnk_080FC908[])(void);
extern u8 gTextGfxBuffer[];

#endif // FILESELECT_H