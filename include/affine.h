#ifndef AFFINE_H
#define AFFINE_H

#include "gba/types.h"

typedef struct {
    s16 x;
    s16 y;
    u16 _4;
    u16 _6;
    u16 _8;
} OAMCommand;
extern OAMCommand gOamCmd;

extern void FlushSprites(void);
extern void CopyOAM(void);
extern void DrawEntities(void);
extern void sub_080ADA04(OAMCommand*, void*);
extern void DrawDirect(u32 spriteIndex, u32 frameIndex);

#endif // AFFINE_H
