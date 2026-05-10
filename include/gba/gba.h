#ifndef GBA_H
#define GBA_H

#include "defines.h"
#include "io_reg.h"
#include "isagbprint.h"
#include "macro.h"
#include "multi_boot.h"
#ifdef PC_PORT
#include "port/port_gba_mem.h"
#include "port/port_offset_USA.h"
#endif
#include "syscall.h"
#include "types.h"
#include <string.h>

#endif // GBA_H
