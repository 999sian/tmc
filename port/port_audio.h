#ifndef PORT_AUDIO_H
#define PORT_AUDIO_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL.h>

bool Port_Audio_Init(void);
void Port_Audio_Shutdown(void);
void Port_Audio_Reset(void);
void Port_Audio_OnFifoWrite(uint32_t addr, uint32_t value);

/* GBA-accurate audio toggle (F8 → Audio). This is the single front door:
 * it records the flag the audio thread reads to bypass the output-DSP
 * post-process chain, AND forwards to the synth backend (NEAREST resampling,
 * no forced reverb). Default off = enhanced (SINC + full DSP chain). */
void Port_Audio_SetGbaAccurate(bool accurate);
bool Port_Audio_IsGbaAccurate(void);

#endif
