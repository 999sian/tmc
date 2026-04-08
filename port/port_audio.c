#include "port_audio.h"

#include "main.h"
#include "port_m4a_backend.h"

enum {
    PORT_AUDIO_SAMPLE_RATE = 48000,
    PORT_AUDIO_CHANNELS = 2,
    PORT_AUDIO_BYTES_PER_FRAME = sizeof(int16_t) * PORT_AUDIO_CHANNELS,
};

static SDL_AudioStream* sAudioStream;

extern Main gMain;

static void Port_Audio_Feed(void* userdata, SDL_AudioStream* stream, int additional_amount, int total_amount) {
    int remaining = additional_amount;
    (void)userdata;
    (void)total_amount;

    while (remaining > 0) {
        int frames = remaining / PORT_AUDIO_BYTES_PER_FRAME;
        int16_t buffer[1024 * PORT_AUDIO_CHANNELS];

        if (frames <= 0) {
            break;
        }
        if (frames > 1024) {
            frames = 1024;
        }

        Port_M4A_Backend_Render(buffer, (uint32_t)frames, gMain.muteAudio);
        SDL_PutAudioStreamData(stream, buffer, frames * PORT_AUDIO_BYTES_PER_FRAME);
        remaining -= frames * PORT_AUDIO_BYTES_PER_FRAME;
    }
}

bool Port_Audio_Init(void) {
    SDL_AudioSpec spec;

    SDL_zero(spec);
    spec.freq = PORT_AUDIO_SAMPLE_RATE;
    spec.format = SDL_AUDIO_S16LE;
    spec.channels = PORT_AUDIO_CHANNELS;

    if (!Port_M4A_Backend_Init(PORT_AUDIO_SAMPLE_RATE)) {
        return false;
    }

    sAudioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Port_Audio_Feed, NULL);
    if (sAudioStream == NULL) {
        SDL_Log("Port_Audio_Init: SDL_OpenAudioDeviceStream failed: %s", SDL_GetError());
        return false;
    }

    if (!SDL_ResumeAudioStreamDevice(sAudioStream)) {
        SDL_Log("Port_Audio_Init: SDL_ResumeAudioStreamDevice failed: %s", SDL_GetError());
        SDL_DestroyAudioStream(sAudioStream);
        sAudioStream = NULL;
        return false;
    }

    return true;
}

void Port_Audio_Shutdown(void) {
    if (sAudioStream != NULL) {
        SDL_DestroyAudioStream(sAudioStream);
        sAudioStream = NULL;
    }

    Port_M4A_Backend_Shutdown();
}

void Port_Audio_Reset(void) {
    Port_M4A_Backend_Reset();
}

void Port_Audio_OnFifoWrite(uint32_t addr, uint32_t value) {
    (void)addr;
    (void)value;
}
