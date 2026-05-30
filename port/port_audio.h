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

/* Mid/side stereo-widen gain (enhanced path only). Range [1.00, 1.50];
 * 1.00 = mono image, 1.20 = shipped default. Mid is never scaled, so mono
 * fold-down is unchanged at any value. */
void Port_Audio_SetWidth(float width);
float Port_Audio_GetWidth(void);

#endif
