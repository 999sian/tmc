/* TMC_AUDIO_AUDIT=<out.tsv>: offline audio audit (repro harness builds only).
 *
 * Renders every BGM through the shipped path (agbplay backend -> output DSP
 * chain) without an SDL device, twice: alone, and with a sound effect started
 * every 250 ms on top (the dense case that drives the mix past the knee).
 * One TSV row per pass:
 *
 *   song pass prePeak preKnee% preOver% postPeak postFull% dc clicks muteStep stopStep
 *
 * prePeak: loudest float sample the synth hands the chain (int16 scale).
 * preKnee%: share of samples past the soft-clip knee (shaped, not flat).
 * preOver%: share past int16 full scale (the chain's saturator decides them).
 * postFull%: share of output samples within 1% of full scale (audible clip).
 * dc: mean output sample. clicks: output steps > 12000 between neighbours.
 * muteStep / stopStep: how far the largest output step in the 10 ms after a
 * mute toggle and after m4aMPlayAllStop exceeds the largest step of the
 * 17 ms before it (the signal's own edges); a hard cut reads as the level.
 *
 * Run headless with --no-audio so no SDL callback races the offline render:
 *   TMC_AUDIO_AUDIT=out.tsv SDL_VIDEODRIVER=dummy ./tmc_pc --no-audio */
#include "global.h"
#include "main.h"
#include "sound.h"
#include "gba/m4a.h"
#include "port_m4a_backend.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

void Port_Audio_PostProcessForAudit(const float* in, int16_t* out, int frames);

enum { kRate = 48000, kChunk = 800, kSeconds = 40 };

typedef struct {
    double prePeak, postPeak, dcSum;
    long n, knee, over, full, clicks;
    int lastL, lastR;
} AudioStats;

static void Stats_Add(AudioStats* s, const float* in, const int16_t* out, int frames) {
    for (int i = 0; i < frames * 2; i++) {
        const double a = fabs(in[i]);
        const int y = out[i];
        const int prev = (i & 1) ? s->lastR : s->lastL;
        if (a > s->prePeak)
            s->prePeak = a;
        if (abs(y) > s->postPeak)
            s->postPeak = abs(y);
        s->knee += a > 25000.0;
        s->over += a > 32767.0;
        s->full += abs(y) >= 32440;
        s->clicks += s->n > 1 && abs(y - prev) > 12000;
        s->dcSum += y;
        if (i & 1)
            s->lastR = y;
        else
            s->lastL = y;
        s->n++;
    }
}

/* Largest sample-to-sample output step over the next 10 ms, starting from the
 * last sample already rendered. */
static int RenderStep(int16_t lastL, int16_t lastR, bool mute) {
    float in[480 * 2];
    int16_t out[480 * 2];
    int maxStep = 0;
    Port_M4A_Backend_Render(in, 480, mute);
    Port_Audio_PostProcessForAudit(in, out, 480);
    for (int i = 0; i < 480 * 2; i++) {
        const int prev = i < 2 ? (i ? lastR : lastL) : out[i - 2];
        if (abs(out[i] - prev) > maxStep)
            maxStep = abs(out[i] - prev);
    }
    return maxStep;
}

static int MaxStep(const int16_t* out, int frames) {
    int m = 0;
    for (int i = 2; i < frames * 2; i++)
        if (abs(out[i] - out[i - 2]) > m)
            m = abs(out[i] - out[i - 2]);
    return m;
}

static void RunPass(FILE* f, u16 song, int withSfx) {
    static float in[kChunk * 2];
    static int16_t out[kChunk * 2];
    AudioStats s = { 0 };
    u16 sfx = SFX_ITEM_GET;
    int muteStep, stopStep;

    m4aMPlayAllStop();
    Port_M4A_Backend_Reset();
    m4aSongNumStart(song);
    for (int c = 0; c < kSeconds * kRate / kChunk; c++) {
        if (withSfx && c % 15 == 0) {
            m4aSongNumStart(sfx);
            sfx = sfx >= SFX_221 ? SFX_ITEM_GET : sfx + 1;
        }
        m4aSoundMain();
        Port_M4A_Backend_Render(in, kChunk, false);
        Port_Audio_PostProcessForAudit(in, out, kChunk);
        Stats_Add(&s, in, out, kChunk);
    }
    muteStep = RenderStep(out[kChunk * 2 - 2], out[kChunk * 2 - 1], true) - MaxStep(out, kChunk);
    RenderStep(0, 0, false); /* back on (also a step, from silence) */
    Port_M4A_Backend_Render(in, kChunk, false);
    Port_Audio_PostProcessForAudit(in, out, kChunk);
    m4aMPlayAllStop();
    stopStep = RenderStep(out[kChunk * 2 - 2], out[kChunk * 2 - 1], false) - MaxStep(out, kChunk);
    muteStep = muteStep > 0 ? muteStep : 0;
    stopStep = stopStep > 0 ? stopStep : 0;

    fprintf(f, "%u\t%s\t%.0f\t%.3f\t%.3f\t%.0f\t%.3f\t%.1f\t%ld\t%d\t%d\n", song, withSfx ? "sfx" : "bgm", s.prePeak,
            100.0 * s.knee / s.n, 100.0 * s.over / s.n, s.postPeak, 100.0 * s.full / s.n, s.dcSum / s.n, s.clicks,
            muteStep, stopStep);
    fflush(f);
}

void Port_ReproAudio_RunIfRequested(void) {
    const char* path = getenv("TMC_AUDIO_AUDIT");
    FILE* f;
    if (!path || !*path)
        return;
    f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "[audio-audit] cannot write %s\n", path);
        _Exit(2);
    }
    Port_M4A_Backend_Init(kRate);
    m4aSoundInit();
    fprintf(f, "song\tpass\tprePeak\tpreKnee%%\tpreOver%%\tpostPeak\tpostFull%%\tdc\tclicks\tmuteStep\tstopStep\n");
    for (u16 song = 1; song < NUM_BGM; song++) {
        RunPass(f, song, 0);
        RunPass(f, song, 1);
        fprintf(stderr, "[audio-audit] song %u\n", song);
    }
    fclose(f);
    fprintf(stderr, "[audio-audit] done\n");
    _Exit(0);
}
