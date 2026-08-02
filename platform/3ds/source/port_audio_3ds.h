#ifndef TMC_PORT_AUDIO_3DS_H
#define TMC_PORT_AUDIO_3DS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct PortAudio3DSStats {
    uint64_t pumps;
    uint64_t buffersRendered;
    uint64_t underrunObservations;
    uint64_t renderTicks;
    uint64_t renderLastTicks;
    uint64_t renderMaxTicks;
    uint32_t sampleRate;
    uint32_t bufferFrames;
    uint32_t bufferCount;
    uint32_t samplePosition;
    uint32_t freeBuffers;
    uint32_t queuedBuffers;
    uint32_t playingBuffers;
    uint32_t doneBuffers;
    uint32_t maxBuffersPerPump;
    bool initialized;
    bool channelPlaying;
} PortAudio3DSStats;

void Port_Audio_3DSPump(void);
void Port_Audio_3DSGetStats(PortAudio3DSStats* stats);

#endif
