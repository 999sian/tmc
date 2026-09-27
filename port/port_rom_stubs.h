/*
 * port_rom_stubs.h — Runtime ROM data stubs for PC port
 *
 * Populates GBA ROM data symbols at runtime from the user's ROM.
 * Contains no copyrighted ROM data.
 */

#ifndef PORT_ROM_STUBS_H
#define PORT_ROM_STUBS_H

#include "port_types.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void Port_InitRomStubs(const u8* romData, u32 romSize);

#ifdef __cplusplus
}
#endif

#endif /* PORT_ROM_STUBS_H */
