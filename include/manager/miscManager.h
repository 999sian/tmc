#ifndef MISCMANAGER_H
#define MISCMANAGER_H

#include "manager.h"

typedef struct {
    Manager base;
    u8 unk_20[0x18];
    s16 x;
    s16 y;
    u16 flag1;
    u16 flag2;
} MiscManager;

#endif // MISCMANAGER_H
