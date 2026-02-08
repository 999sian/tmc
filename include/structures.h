#ifndef STRUCTURES_H
#define STRUCTURES_H

#include "global.h"

typedef struct {
    u8 numTiles;
    u8 unk_1;
    u16 firstTileIndex;
} SpriteFrame;

typedef struct {
    void* animations;
    SpriteFrame* frames;
    void* ptr;
    u32 pad;
} SpritePtr;

extern const SpritePtr gSpritePtrs[];

typedef struct {
    u8 frame;
    u8 frameIndex;
} PACKED FrameStruct;

#endif // STRUCTURES_H
