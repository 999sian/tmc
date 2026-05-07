#ifndef PORT_M4A_BACKEND_H
#define PORT_M4A_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

typedef struct SongHeader SongHeader;

#ifdef __cplusplus
extern "C" {
#endif

bool Port_M4A_Backend_Init(uint32_t sampleRate);
void Port_M4A_Backend_Shutdown(void);
void Port_M4A_Backend_Reset(void);
void Port_M4A_Backend_SoundInit(uint32_t soundMode);
void Port_M4A_Backend_SetSoundMode(uint32_t soundMode);
void Port_M4A_Backend_SetVSyncEnabled(bool enabled);
bool Port_M4A_Backend_StartSongById(uint8_t playerIndex, uint16_t songId);
void Port_M4A_Backend_StartSong(uint8_t playerIndex, const SongHeader* songHeader);
void Port_M4A_Backend_StopPlayer(uint8_t playerIndex);
void Port_M4A_Backend_ContinuePlayer(uint8_t playerIndex);
void Port_M4A_Backend_SetTrackVolume(uint8_t playerIndex, uint16_t trackBits, uint16_t volume);
void Port_M4A_Backend_SetTrackPan(uint8_t playerIndex, uint16_t trackBits, int8_t pan);
void Port_M4A_Backend_Render(int16_t* outSamples, uint32_t frameCount, bool mute);
const char* Port_GetSongLabel(uint16_t songId);
/* Returns true while the player still has tracks actively running (i.e. song
 * hasn't reached its `ply_fine`). Used to detect when a high-priority SFX has
 * finished so the BGM can be unmuted. */
bool Port_M4A_Backend_IsPlayerActive(uint8_t playerIndex);

#ifdef __cplusplus
}
#endif

#endif
