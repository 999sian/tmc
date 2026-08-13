#include "old3ds_frame_pacer.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static void TestFullRateNeverSkips(void) {
    Old3DSFramePacer pacer;
    Old3DSFramePacer_Init(&pacer, 6000);
    uint64_t sleep = 99;
    for (uint64_t now = 0; now <= 2000; now += 100) {
        assert(Old3DSFramePacer_BeginTick(&pacer, now, &sleep));
        assert(sleep == 0);
    }
    assert(pacer.skippedPresentations == 0);
    assert(pacer.activeIntervals == 20);
    assert(pacer.activeElapsedTicks == 2000);
}

static void TestThirtyFpsAlternatesPresentation(void) {
    Old3DSFramePacer pacer;
    Old3DSFramePacer_Init(&pacer, 6000);
    uint64_t now = 0;
    uint64_t sleep;
    unsigned presents = 0;
    unsigned skips = 0;

    for (unsigned i = 0; i < 12; ++i) {
        const bool present = Old3DSFramePacer_BeginTick(&pacer, now, &sleep);
        presents += present ? 1u : 0u;
        skips += present ? 0u : 1u;
        now += present ? 200u : sleep;
    }
    assert(presents == 6);
    assert(skips == 6);
    assert(pacer.maxConsecutiveSkipsSeen == 1);
}

static void TestFortyFpsKeepsMorePictures(void) {
    Old3DSFramePacer pacer;
    Old3DSFramePacer_Init(&pacer, 6000);
    uint64_t now = 0;
    uint64_t sleep;
    unsigned presents = 0;
    unsigned skips = 0;

    for (unsigned i = 0; i < 15; ++i) {
        const bool present = Old3DSFramePacer_BeginTick(&pacer, now, &sleep);
        presents += present ? 1u : 0u;
        skips += present ? 0u : 1u;
        now += present ? 150u : sleep;
    }
    assert(presents == 10);
    assert(skips == 5);
}

static void TestSkipCapAndPauseResync(void) {
    Old3DSFramePacer pacer;
    Old3DSFramePacer_Init(&pacer, 6000);
    uint64_t sleep;
    assert(Old3DSFramePacer_BeginTick(&pacer, 0, &sleep));
    assert(!Old3DSFramePacer_BeginTick(&pacer, 300, &sleep));
    assert(!Old3DSFramePacer_BeginTick(&pacer, 300, &sleep));
    assert(Old3DSFramePacer_BeginTick(&pacer, 300, &sleep));
    assert(pacer.maxConsecutiveSkipsSeen == 2);

    assert(Old3DSFramePacer_BeginTick(&pacer, 1000, &sleep));
    assert(pacer.resyncs == 1);
    assert(pacer.presentationDebtTicks == 0);
    assert(pacer.activeIntervals == 3);
    assert(pacer.activeElapsedTicks == 300);
}

static void TestSmallOverrunGetsRemainderSleep(void) {
    Old3DSFramePacer pacer;
    Old3DSFramePacer_Init(&pacer, 6000);
    uint64_t sleep;
    assert(Old3DSFramePacer_BeginTick(&pacer, 0, &sleep));
    assert(!Old3DSFramePacer_BeginTick(&pacer, 190, &sleep));
    assert(sleep == 10);
}

static void TestCadenceOnlyPathExcludesPause(void) {
    Old3DSFramePacer pacer;
    Old3DSFramePacer_Init(&pacer, 6000);
    assert(Old3DSFramePacer_RecordActiveInterval(&pacer, 100));
    assert(!Old3DSFramePacer_RecordActiveInterval(&pacer, 600));
    assert(pacer.activeIntervals == 1);
    assert(pacer.activeElapsedTicks == 100);
    assert(pacer.skippedPresentations == 0);
}

int main(void) {
    TestFullRateNeverSkips();
    TestThirtyFpsAlternatesPresentation();
    TestFortyFpsKeepsMorePictures();
    TestSkipCapAndPauseResync();
    TestSmallOverrunGetsRemainderSleep();
    TestCadenceOnlyPathExcludesPause();
    puts("old3ds_frame_pacer_test: PASS");
    return 0;
}
