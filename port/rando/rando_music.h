/*
 * Area-music shuffle runtime (MUSIC_RANDO).
 *
 * The GBA randomizer patches the per-area song byte in the ROM's area-metadata
 * table (EU: 0x12746b + 4*area); natively that byte is
 * gAreaMetadata[area].queueBgm, read in exactly one place (LoadRoomBgm).
 * This remap is the runtime equivalent of that table patch.
 */

#ifndef PORT_RANDO_MUSIC_H
#define PORT_RANDO_MUSIC_H

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the shuffled song id for `area` when a seed is active and
 * MUSIC_RANDO assigned one; otherwise returns `song` (vanilla) unchanged.
 * Out-of-range assignments fall back to vanilla with one warning per area. */
int Rando_Music_Remap(int area, int song);

#ifdef __cplusplus
}
#endif

#endif /* PORT_RANDO_MUSIC_H */
