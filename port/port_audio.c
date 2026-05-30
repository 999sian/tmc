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

/* GBA-accurate audio toggle. Written on the main thread (F8 menu), read on the
 * SDL audio thread (Port_Audio_Feed) to decide whether to bypass the output-DSP
 * post-process chain below. The store uses RELEASE and the audio-thread load
 * uses ACQUIRE so that when the main thread clears the filter history on the
 * false→true edge (see Port_Audio_SetGbaAccurate), there is a happens-before
 * edge guaranteeing the audio thread has already observed accurate==true and
 * stopped touching the filter statics. */
static bool sGbaAccurate = false;

/* Mid/side widen gain for post-process stage 3 (F8 → Audio "Stereo width").
 * Default 1.20 = the long-standing shipped image. Written on the main thread,
 * read once per PostProcess call on the audio thread; relaxed atomics suffice
 * (a rarely-changing float that touches no other state, and the widen is
 * memoryless so a torn transition is at worst one slightly-off buffer). The
 * mid is never scaled, so mono fold-down equals the original mix at any value. */
static float sWidth = 1.20f;

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
 *      values cluster around centre. Boosting the side component opens
 *      the stereo image without altering the mid, so mono playback
 *      collapses cleanly to the original mix. The side gain defaults to
 *      1.20 (the long-standing shipped value) and is runtime-settable
 *      via the F8 → Audio "Stereo width" slider (sWidth).
 *
 *   4. Soft saturation via tanh-style curve — peaks above 25 000 round
 *      off smoothly instead of flattening, preserving perceived loudness
 *      on dense passages (multiple lead voices). This is intentionally
 *      the ONLY dynamics processing: no compressor/limiter, because a
 *      time-constant'd glue stage would pump on TMC's dense ~60 Hz
 *      transient stream and smear the staccato PSG snap. There is no
 *      makeup gain — the upstream agbplay float mix is already bounded
 *      before the int16 cast, so the chain only ever shapes, never lifts.
 *
 * Any musical reverb lives SYNTH-SIDE (agbplay's PCM-only comb, F8 →
 * Audio "Reverb"), NOT here: an output-stage reverb would act on the
 * already-summed mix and smear the dry PSG leads and SFX. Do not add a
 * reverb to this chain.
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
 *   a1 = -2 * cos_w      / a0     (= +0.620203 — POSITIVE here because
 *                                  cos_w is negative)
 *   a2 = (1 - alpha)     / a0
 *   a0 = 1 + alpha = 1.612372
 *
 * Issue #115: kLpA1 was previously stored with the wrong sign, giving
 * a DC gain of ~3.0 — the soft-clip stage then squashed every loud
 * passage, producing the "horribly compressed" intro audio reported
 * on Linux Bazzite. The canonical sign restores unity DC gain. */
static const float kLpB0 =  0.4651777f;
static const float kLpB1 =  0.9303554f;
static const float kLpB2 =  0.4651777f;
static const float kLpA1 =  0.6202032f;
static const float kLpA2 =  0.2403461f;
static float sLpX1L = 0.0f, sLpX2L = 0.0f, sLpY1L = 0.0f, sLpY2L = 0.0f;
static float sLpX1R = 0.0f, sLpX2R = 0.0f, sLpY1R = 0.0f, sLpY2R = 0.0f;

/* Zero the HPF/LPF biquad history. Single source of truth so the reset path
 * and the GBA-accurate toggle (which clears on entry to keep re-engagement
 * click-free) can't drift if a future IIR stage adds more state. */
static inline void Port_Audio_ClearFilterState(void) {
    sHpPrevInL = sHpPrevInR = sHpPrevOutL = sHpPrevOutR = 0.0f;
    sLpX1L = sLpX2L = sLpY1L = sLpY2L = 0.0f;
    sLpX1R = sLpX2R = sLpY1R = sLpY2R = 0.0f;
}

static inline float Port_Audio_SoftClip(float x) {
    /* Smooth saturation: near-linear up to |25000|, rolling off gradually past
     * that and asymptoting at ±32767.
     *
     * The bend uses tanh rather than atan. tanh(0)=0 with derivative 1, so the
     * curve joins the linear region with *matching slope* (C1-continuous) — a
     * smooth knee. The previous atan bend had slope 1 below the threshold but
     * 2/pi≈0.637 just above it; that slope discontinuity is a corner in the
     * transfer curve, which injects odd-order harmonics on every sample that
     * crosses the knee — an audible edgy fizz on exactly the loud lead notes
     * this stage is meant to protect. tanh(∞)=1 keeps the asymptote at exactly
     * 25000+7767 = 32767, so the int16 cast at the call site can never overflow. */
    const float kThreshold = 25000.0f;
    if (x >= -kThreshold && x <= kThreshold) {
        return x;
    }
    const float sign = x < 0 ? -1.0f : 1.0f;
    const float over = (x * sign) - kThreshold;        /* > 0 */
    const float room = 32767.0f - kThreshold;          /* 7767 */
    const float bent = room * tanhf(over / room);
    return sign * (kThreshold + bent);
}

static void Port_Audio_PostProcess(int16_t* buffer, int frames) {
    /* Read the width gain once per call (not per sample). __atomic_load (the
       generic form) is used because __atomic_load_n rejects float. Clamp
       defensively to the slider's range in case of an out-of-band write. */
    float w;
    __atomic_load(&sWidth, &w, __ATOMIC_RELAXED);
    if (w < 1.0f) w = 1.0f;
    if (w > 1.5f) w = 1.5f;

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
        const float side = (lpL - lpR) * 0.5f * w;
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

        /* gMain.muteAudio is written on the main thread; read it with a relaxed
           atomic load here on the SDL audio thread to avoid a data race. It is a
           rarely-changing u8 flag, so relaxed ordering is sufficient. */
        Port_M4A_Backend_Render(buffer, (uint32_t)frames,
                                __atomic_load_n(&gMain.muteAudio, __ATOMIC_RELAXED) != 0);
        /* GBA-accurate mode hands the synth output straight to SDL — no
           HPF/LPF/widen/soft-clip coloring — for A/B comparison against
           hardware/mGBA. The synth-side knobs (NEAREST resampling) are set
           separately via the backend. */
        if (!__atomic_load_n(&sGbaAccurate, __ATOMIC_ACQUIRE)) {
            Port_Audio_PostProcess(buffer, frames);
        }
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

    /* Clear filter history so a reset doesn't carry stale DC-blocker / low-pass
       state into the next buffer (a benign one-buffer transient otherwise). */
    Port_Audio_ClearFilterState();
}

void Port_Audio_SetGbaAccurate(bool accurate) {
    const bool prev = __atomic_load_n(&sGbaAccurate, __ATOMIC_RELAXED);
    __atomic_store_n(&sGbaAccurate, accurate, __ATOMIC_RELEASE);

    /* On the false→true edge the audio thread stops running PostProcess (it
       observes the flag via the ACQUIRE load in Feed), so the filter statics are
       no longer read — clear them now so a later return to the enhanced chain
       resumes from zero history instead of state lagged by the raw buffers that
       ran in between, which would click/ring on the first re-engaged buffer and
       contaminate the very A/B the toggle exists for. Clear only on this edge:
       on true→false the chain restarts from already-cleared state. */
    if (!prev && accurate) {
        Port_Audio_ClearFilterState();
    }
    Port_M4A_Backend_SetGbaAccurate(accurate);
}

bool Port_Audio_IsGbaAccurate(void) {
    return __atomic_load_n(&sGbaAccurate, __ATOMIC_ACQUIRE);
}

void Port_Audio_SetWidth(float width) {
    if (width < 1.0f) width = 1.0f;
    if (width > 1.5f) width = 1.5f;
    __atomic_store(&sWidth, &width, __ATOMIC_RELAXED);
}

float Port_Audio_GetWidth(void) {
    float w;
    __atomic_load(&sWidth, &w, __ATOMIC_RELAXED);
    return w;
}

void Port_Audio_OnFifoWrite(uint32_t addr, uint32_t value) {
    (void)addr;
    (void)value;
}
