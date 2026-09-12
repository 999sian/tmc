#ifndef PORT_SPRITE_REGION_H
#define PORT_SPRITE_REGION_H

#include "port_config.h"

/* Convert a USA logical ID only at a compiled-data boundary. ROM tables and
 * entity spriteIndex fields already contain native IDs and must not pass here. */
static inline u16 Port_LogicalSpriteIndex(u16 index) {
    if (gRomRegion == ROM_REGION_EU) {
        if (index == 288)
            return 0xffff; /* OBJECTB4_1 is absent from EU. */
        if (index > 288)
            return index - 1;
    }
    return index;
}

/* Sprites enum values themselves omit entry 288 in an EU compilation. */
static inline u16 Port_CompiledSpriteIndex(u16 index) {
#ifdef EU
    if (index >= 288 && index != 0xffff)
        index++;
#endif
    return Port_LogicalSpriteIndex(index);
}

#endif
