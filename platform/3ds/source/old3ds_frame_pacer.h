#ifndef TMC_OLD3DS_FRAME_PACER_H
#define TMC_OLD3DS_FRAME_PACER_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Old 3DS rendering can take more than one 60 Hz frame period.  Keep that
 * wall-clock overrun as presentation debt so a later game tick may reuse the
 * previous picture instead of slowing the game simulation down with it.
 *
 * This state machine contains no libctru calls, which keeps the scheduling
 * policy deterministic and host-testable.  The platform layer performs the
 * optional sleep returned for a skipped presentation.
 */
typedef struct Old3DSFramePacer {
    uint64_t framePeriodTicks;
    uint64_t skipThresholdTicks;
    uint64_t lastBoundaryTick;
    int64_t presentationDebtTicks;
    uint32_t consecutiveSkips;
    uint32_t maxConsecutiveSkipsSeen;
    uint64_t skippedPresentations;
    uint64_t sleepRequests;
    uint64_t requestedSleepTicks;
    uint64_t activeElapsedTicks;
    uint64_t activeIntervals;
    uint64_t resyncs;
    bool initialized;
} Old3DSFramePacer;

void Old3DSFramePacer_Init(Old3DSFramePacer* pacer, uint64_t ticksPerSecond);
bool Old3DSFramePacer_RecordActiveInterval(Old3DSFramePacer* pacer, uint64_t elapsedTicks);
bool Old3DSFramePacer_BeginTick(Old3DSFramePacer* pacer, uint64_t nowTick,
                               uint64_t* skippedTickSleep);

#endif
