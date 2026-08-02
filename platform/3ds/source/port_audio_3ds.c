#include "port_audio.h"
#include "port_audio_3ds.h"
#include "port_m4a_backend.h"

#include <3ds.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define SAMPLE_RATE 32728
#define BUFFER_FRAMES 1024
#define BUFFER_COUNT 4

static ndspWaveBuf sWave[BUFFER_COUNT];
static int16_t* sSamples;
static bool sInitialized;
static float sVolume = 1.0f;
static PortAudio3DSStats sStats;

static void FillBuffer(int index) {
    const uint64_t startTick = svcGetSystemTick();
    int16_t* dst = sSamples + index * BUFFER_FRAMES * 2;
    Port_M4A_Backend_Render(dst, BUFFER_FRAMES, false);
    if (sVolume < 0.999f) {
        for (int i = 0; i < BUFFER_FRAMES * 2; ++i) dst[i] = (int16_t)(dst[i] * sVolume);
    }
    DSP_FlushDataCache(dst, BUFFER_FRAMES * 2 * sizeof(int16_t));
    sWave[index].data_pcm16 = dst;
    sWave[index].nsamples = BUFFER_FRAMES;
    sWave[index].status = NDSP_WBUF_FREE;
    ndspChnWaveBufAdd(0, &sWave[index]);
    const uint64_t elapsed = svcGetSystemTick() - startTick;
    ++sStats.buffersRendered;
    sStats.renderTicks += elapsed;
    sStats.renderLastTicks = elapsed;
    if (elapsed > sStats.renderMaxTicks) sStats.renderMaxTicks = elapsed;
}

bool Port_Audio_Init(void) {
    if (sInitialized) return true;
    if (ndspInit() != 0) return false;
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, SAMPLE_RATE);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    float mix[12] = { 1.0f, 0.0f, 0.0f, 1.0f };
    ndspChnSetMix(0, mix);

    sSamples = (int16_t*)linearAlloc(BUFFER_COUNT * BUFFER_FRAMES * 2 * sizeof(int16_t));
    if (!sSamples || !Port_M4A_Backend_Init(SAMPLE_RATE)) {
        if (sSamples) linearFree(sSamples);
        sSamples = NULL;
        ndspExit();
        return false;
    }
    memset(sWave, 0, sizeof(sWave));
    memset(sSamples, 0, BUFFER_COUNT * BUFFER_FRAMES * 2 * sizeof(int16_t));
    memset(&sStats, 0, sizeof(sStats));
    sStats.sampleRate = SAMPLE_RATE;
    sStats.bufferFrames = BUFFER_FRAMES;
    sStats.bufferCount = BUFFER_COUNT;
    sStats.initialized = true;
    sInitialized = true;
    for (int i = 0; i < BUFFER_COUNT; ++i) FillBuffer(i);
    return true;
}

void Port_Audio_3DSPump(void) {
    if (!sInitialized) return;
    ++sStats.pumps;
    uint32_t refillCount = 0;
    if (!ndspChnIsPlaying(0)) ++sStats.underrunObservations;
    for (int i = 0; i < BUFFER_COUNT; ++i) {
        if (sWave[i].status == NDSP_WBUF_DONE) {
            FillBuffer(i);
            ++refillCount;
        }
    }
    if (refillCount > sStats.maxBuffersPerPump) sStats.maxBuffersPerPump = refillCount;
}

void Port_Audio_3DSGetStats(PortAudio3DSStats* stats) {
    if (!stats) return;
    *stats = sStats;
    stats->initialized = sInitialized;
    stats->samplePosition = sInitialized ? ndspChnGetSamplePos(0) : 0;
    stats->channelPlaying = sInitialized && ndspChnIsPlaying(0);
    stats->freeBuffers = 0;
    stats->queuedBuffers = 0;
    stats->playingBuffers = 0;
    stats->doneBuffers = 0;
    for (int i = 0; i < BUFFER_COUNT; ++i) {
        switch (sWave[i].status) {
            case NDSP_WBUF_FREE: ++stats->freeBuffers; break;
            case NDSP_WBUF_QUEUED: ++stats->queuedBuffers; break;
            case NDSP_WBUF_PLAYING: ++stats->playingBuffers; break;
            case NDSP_WBUF_DONE: ++stats->doneBuffers; break;
        }
    }
}

void Port_Audio_Shutdown(void) {
    if (!sInitialized) return;
    ndspChnWaveBufClear(0);
    Port_M4A_Backend_Shutdown();
    linearFree(sSamples);
    sSamples = NULL;
    ndspExit();
    sInitialized = false;
    sStats.initialized = false;
}

void Port_Audio_Reset(void) { Port_M4A_Backend_Reset(); }
void Port_Audio_SetGbaAccurate(bool accurate) { Port_M4A_Backend_SetGbaAccurate(accurate); }
bool Port_Audio_IsGbaAccurate(void) { return Port_M4A_Backend_GetGbaAccurate(); }
void Port_Audio_SetWidth(float width) { (void)width; }
float Port_Audio_GetWidth(void) { return 1.0f; }
void Port_Audio_SetReverbLevel(int level) { Port_M4A_Backend_SetReverbLevel(level); }
int Port_Audio_GetReverbLevel(void) { return Port_M4A_Backend_GetReverbLevel(); }
void Port_Audio_SetMasterVolume(float volume) {
    sVolume = fmaxf(0.0f, fminf(1.0f, volume));
    Port_M4A_Backend_SetMasterVolume(sVolume);
}
float Port_Audio_GetMasterVolume(void) { return sVolume; }
