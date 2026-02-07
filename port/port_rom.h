#pragma once
#include "port_types.h"

// ROM data buffer
extern u8* gRomData;
extern u32 gRomSize;

// Load the ROM file and set up ROM-backed symbols
void Port_LoadRom(const char* path);

// ROM access logging - logs unique ROM addresses accessed at runtime
void Port_LogRomAccess(u32 gba_addr, const char* caller);
void Port_PrintRomAccessSummary(void);
