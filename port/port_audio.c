#include "port_audio.h"

#include "main.h"
#include "port_m4a_backend.h"

#include <math.h>

enum {
    PORT_AUDIO_SAMPLE_RATE = 48000,
    PORT_AUDIO_CHANNELS = 2,
    PORT_AUDIO_BYTES_PER_FRAME = sizeof(int16_t) * PORT_AUDIO_CHANNELS,
};

static SDL_AudioStream* sAudioStream;

/* ---- Output DSP post-processing chain ------------------------------
 *
 * Runs after agbplay's MP2K render, before the buffer is handed to
 * SDL. The chain is:
 *
 *   1. DC-blocking high-pass at ~30 Hz. Removes any DC offset
 *      introduced by the synthesis path and subsonic energy that no
 *      consumer speaker can reproduce.
 *
 *   2. Two-pole Butterworth low-pass at ~16 kHz. Sharper roll-off
 *      than the previous one-pole and stays effectively flat in band
 *      below ~12 kHz; eliminates the lingering high-frequency
 *      aliasing tail from PCM resampling.
 *
 *   3. Mid/side stereo widening. TMC's MP2K mix is mostly mono — pan
 *      values cluster around centre. A small +20 % boost to the side
 *      component opens the stereo image without altering the mid (so
 *      mono compatibility is preserved).
 *
 *   4. Soft saturation via tanh-style curve. Replaces the previous
 *      hard clip — peaks above ~28 000 round off smoothly instead of
 *      flattening, preserving perceived loudness on dense passages
 *      (reverb tail + multiple lead voices).
 *
 * All stages are sample-rate-fixed at 48 kHz (Port_Audio's output);
 * coefficients are pre-computed constants below. */

/* High-pass: y[n] = a * (y[n-1] + x[n] - x[n-1]); a = 1 - 2*pi*fc/fs.
 * At fc=30, fs=48000 → a ≈ 0.99607. */
static const float kHpAlpha = 0.99607f;
static float sHpPrevInL  = 0.0f, sHpPrevInR  = 0.0f;
static float sHpPrevOutL = 0.0f, sHpPrevOutR = 0.0f;

/* Two-pole Butterworth low-pass biquad at fc=16 kHz, fs=48 kHz, Q=0.707.
 * Coefficients computed offline:
 *   omega   = 2*pi*16000/48000 = 2.094395
 *   alpha   = sin(omega) / (2*Q) = 0.612372
 *   cos_w   = -0.5
 *   b0 = (1 - cos_w) / 2 / a0
 *   b1 = (1 - cos_w)     / a0
 *   b2 = (1 - cos_w) / 2 / a0
 *   a1 = -2 * cos_w      / a0
 *   a2 = (1 - alpha)     / a0
 *   a0 = 1 + alpha = 1.612372 */
static const float kLpB0 =  0.4651777f;
static const float kLpB1 =  0.9303554f;
static const float kLpB2 =  0.4651777f;
static const float kLpA1 = -0.6202032f;
static const float kLpA2 =  0.2403461f;
static float sLpX1L = 0.0f, sLpX2L = 0.0f, sLpY1L = 0.0f, sLpY2L = 0.0f;
static float sLpX1R = 0.0f, sLpX2R = 0.0f, sLpY1R = 0.0f, sLpY2R = 0.0f;

static inline float Port_Audio_SoftClip(float x) {
    /* Smooth saturation that's near-linear up to ~25000 and rolls off
     * gradually past that, asymptoting at ±32767. */
    const float kThreshold = 25000.0f;
    if (x >= -kThreshold && x <= kThreshold) {
        return x;
    }
    const float sign = x < 0 ? -1.0f : 1.0f;
    const float over = (x * sign) - kThreshold;        /* > 0 */
    const float room = 32767.0f - kThreshold;          /* 7767 */
    /* atan(over / room) maps [0, ∞) to [0, π/2). Scale so output asymptote is `room`. */
    const float bent = (room * (2.0f / 3.14159265f)) * atanf(over / room);
    return sign * (kThreshold + bent);
}

static void Port_Audio_PostProcess(int16_t* buffer, int frames) {
    for (int i = 0; i < frames; ++i) {
        float l = (float)buffer[i * 2 + 0];
        float r = (float)buffer[i * 2 + 1];

        /* 1. High-pass DC blocker. */
        const float hpL = kHpAlpha * (sHpPrevOutL + l - sHpPrevInL);
        const float hpR = kHpAlpha * (sHpPrevOutR + r - sHpPrevInR);
        sHpPrevInL = l; sHpPrevOutL = hpL;
        sHpPrevInR = r; sHpPrevOutR = hpR;

        /* 2. Two-pole low-pass. */
        const float lpL = kLpB0 * hpL + kLpB1 * sLpX1L + kLpB2 * sLpX2L
                        - kLpA1 * sLpY1L - kLpA2 * sLpY2L;
        const float lpR = kLpB0 * hpR + kLpB1 * sLpX1R + kLpB2 * sLpX2R
                        - kLpA1 * sLpY1R - kLpA2 * sLpY2R;
        sLpX2L = sLpX1L; sLpX1L = hpL; sLpY2L = sLpY1L; sLpY1L = lpL;
        sLpX2R = sLpX1R; sLpX1R = hpR; sLpY2R = sLpY1R; sLpY1R = lpR;

        /* 3. Mid/side widen — boost side by 20 %, leave mid alone so
         *    mono playback collapses cleanly to the original mix. */
        const float mid  = (lpL + lpR) * 0.5f;
        const float side = (lpL - lpR) * 0.5f * 1.2f;
        const float wL = mid + side;
        const float wR = mid - side;

        /* 4. Soft-clip + cast back to int16. */
        const float ol = Port_Audio_SoftClip(wL);
        const float or_ = Port_Audio_SoftClip(wR);
        buffer[i * 2 + 0] = (int16_t)ol;
        buffer[i * 2 + 1] = (int16_t)or_;
    }
}

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
        Port_Audio_PostProcess(buffer, frames);
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
