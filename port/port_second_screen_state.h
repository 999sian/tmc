#ifndef PORT_SECOND_SCREEN_STATE_H
#define PORT_SECOND_SCREEN_STATE_H

/*
 * Thread-safe game-state snapshot for the second-screen render thread.
 *
 * Modeled on port_m4a_backend.cpp's sStateMutex pattern: the game thread
 * publishes a small copy of the fields the second screen needs once per
 * tick; the second-screen render thread only ever reads that published
 * copy, never gSave/gRoomControls/gPlayerEntity directly. Lock is held only
 * for a memcpy on both sides — never around anything that renders or
 * blocks, so the second-screen thread can never stall the game thread.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t area;
    uint8_t room;
    int32_t playerX;
    int32_t playerY;
    uint8_t equippedA;
    uint8_t equippedB;
} SecondScreenSnapshot;

/* Called once per game tick from the main loop (src/main.c). Builds a fresh
 * snapshot from gRoomControls/gPlayerEntity/gSave and swaps it in under a
 * short-held lock. No-op off Android (there is no second-screen thread to
 * publish to). */
void Port_SecondScreenState_Publish(void);

/* Called from the second-screen render thread. Copies the most recently
 * published snapshot into `out`. Safe to call even before the first
 * Publish() — returns a zeroed snapshot in that case. */
void Port_SecondScreenState_Read(SecondScreenSnapshot* out);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SECOND_SCREEN_STATE_H */
