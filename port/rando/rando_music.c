/*
 * Area-music shuffle runtime (MUSIC_RANDO). See rando_music.h.
 *
 * Id space: the .logic's Items.Music.0xNN subtypes are the engine's own
 * Sound-enum song ids (0x01 BGM_CASTLE_TOURNAMENT .. 0x3b BGM_WIND_RUINS,
 * include/sound.h); no translation table is needed. Valid BGM ids are
 * 1..NUM_BGM (the SoundReq BGM block; 0 is SFX_NONE, NUM_BGM+1.. are SFX).
 */

#include "rando/rando_music.h"

#include "rando/rando.h"
#include "rando/rando_logic.h"

#include "sound.h" /* NUM_BGM */

#include <stdint.h>
#include <stdio.h>

#define RANDO_MUSIC_MAX_AREAS 256

int Rando_Music_Remap(int area, int song) {
    /* One warning per area, then silent vanilla fallback (LoadRoomBgm runs
     * on every room transition). */
    static uint8_t warned[RANDO_MUSIC_MAX_AREAS / 8];
    int assigned;

    if (!Rando_IsActive() || area < 0 || area >= RANDO_MUSIC_MAX_AREAS) {
        return song;
    }
    assigned = RandoLogic_GetMusicAssignment((uint32_t)area);
    if (assigned < 0) {
        return song; /* MUSIC_RANDO off / no assignment: strict no-op */
    }
    if (assigned == 0 || assigned > NUM_BGM) {
        if (!(warned[area >> 3] & (1u << (area & 7)))) {
            warned[area >> 3] |= (uint8_t)(1u << (area & 7));
            fprintf(stderr,
                    "[RANDO] music: area 0x%02x assigned song 0x%02x outside BGM range "
                    "0x01..0x%02x, keeping vanilla 0x%02x\n",
                    area, assigned, NUM_BGM, song);
        }
        return song;
    }
    return assigned;
}
