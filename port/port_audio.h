#ifndef PORT_AUDIO_H
#define PORT_AUDIO_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL.h>

bool Port_Audio_Init(void);
void Port_Audio_Shutdown(void);
void Port_Audio_Reset(void);
void Port_Audio_OnFifoWrite(uint32_t addr, uint32_t value);

#endif
