#include "old3ds_frame_pacer.h"

#include <limits.h>
#include <string.h>

enum {
    OLD3DS_TARGET_HZ = 60,
    OLD3DS_MAX_CONSECUTIVE_SKIPS = 2,
    OLD3DS_RESYNC_PERIODS = 6,
};

void Old3DSFramePacer_Init(Old3DSFramePacer* pacer, uint64_t ticksPerSecond) {
    if (!pacer) return;
    memset(pacer, 0, sizeof(*pacer));
    pacer->framePeriodTicks = ticksPerSecond / OLD3DS_TARGET_HZ;
    if (pacer->framePeriodTicks == 0) pacer->framePeriodTicks = 1;
    /* A whole missed frame is presentation debt.  The small tolerance catches
     * a nominal two-VBlank frame despite timer quantization without turning a
     * minor VBlank phase difference into constant skipping. */
    pacer->skipThresholdTicks = pacer->framePeriodTicks - pacer->framePeriodTicks / 10u;
}

bool Old3DSFramePacer_RecordActiveInterval(Old3DSFramePacer* pacer, uint64_t elapsedTicks) {
    if (!pacer || pacer->framePeriodTicks == 0 ||
        elapsedTicks >= pacer->framePeriodTicks * OLD3DS_RESYNC_PERIODS) {
        return false;
    }
    if (elapsedTicks > UINT64_MAX - pacer->activeElapsedTicks) {
        pacer->activeElapsedTicks = UINT64_MAX;
    } else {
        pacer->activeElapsedTicks += elapsedTicks;
    }
    if (pacer->activeIntervals != UINT64_MAX) ++pacer->activeIntervals;
    return true;
}

bool Old3DSFramePacer_BeginTick(Old3DSFramePacer* pacer, uint64_t nowTick,
                               uint64_t* skippedTickSleep) {
    if (skippedTickSleep) *skippedTickSleep = 0;
    if (!pacer || pacer->framePeriodTicks == 0) return true;

    if (!pacer->initialized) {
        pacer->initialized = true;
        pacer->lastBoundaryTick = nowTick;
        return true;
    }

    const uint64_t elapsed = nowTick - pacer->lastBoundaryTick;
    pacer->lastBoundaryTick = nowTick;
    if (!Old3DSFramePacer_RecordActiveInterval(pacer, elapsed)) {
        /* HOME/sleep, an SD-card stall, and a quick dump are pauses, not game
         * time to replay in a burst.  Drop their stale debt. */
        pacer->presentationDebtTicks = 0;
        pacer->consecutiveSkips = 0;
        ++pacer->resyncs;
        return true;
    }

    /* Keep the cadence diagnostic on the same active-time window as the
     * scheduler.  Counting a HOME/sleep/dump pause here would make a healthy
     * 60 Hz logic loop look slow for the remainder of the process. */
    int64_t delta;
    if (elapsed >= pacer->framePeriodTicks) {
        const uint64_t positive = elapsed - pacer->framePeriodTicks;
        delta = positive > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)positive;
    } else {
        delta = -(int64_t)(pacer->framePeriodTicks - elapsed);
    }
    if (delta > 0 && pacer->presentationDebtTicks > INT64_MAX - delta) {
        pacer->presentationDebtTicks = INT64_MAX;
    } else {
        pacer->presentationDebtTicks += delta;
    }

    /* Do not let an unusually cheap tick create a long-term speed credit. */
    const int64_t minimumDebt = -(int64_t)pacer->framePeriodTicks;
    if (pacer->presentationDebtTicks < minimumDebt) pacer->presentationDebtTicks = minimumDebt;

    if (pacer->presentationDebtTicks < (int64_t)pacer->skipThresholdTicks ||
        pacer->consecutiveSkips >= OLD3DS_MAX_CONSECUTIVE_SKIPS) {
        pacer->consecutiveSkips = 0;
        return true;
    }

    ++pacer->consecutiveSkips;
    ++pacer->skippedPresentations;
    if (pacer->consecutiveSkips > pacer->maxConsecutiveSkipsSeen) {
        pacer->maxConsecutiveSkipsSeen = pacer->consecutiveSkips;
    }

    /* If the overrun is slightly under a full tick, sleep only the remainder.
     * This prevents a skip from making the simulation run faster than 60 Hz.
     * BeginTick may reach this branch only at >=90% debt, so this compensation
     * is bounded to <=10% of one period (~1.67 ms at the 60 Hz target). */
    if (pacer->presentationDebtTicks < (int64_t)pacer->framePeriodTicks) {
        const uint64_t sleepTicks =
            pacer->framePeriodTicks - (uint64_t)pacer->presentationDebtTicks;
        if (skippedTickSleep) *skippedTickSleep = sleepTicks;
        if (sleepTicks != 0) {
            ++pacer->sleepRequests;
            pacer->requestedSleepTicks += sleepTicks;
        }
    }
    return false;
}
