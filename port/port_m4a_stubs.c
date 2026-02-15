#include "gba/m4a.h"
#include <stdbool.h>
#include <string.h>

#define M4A_PLAYER_COUNT 32
#define M4A_MAX_TRACKS 16

#define MUSICPLAYER_STATUS_TRACK 0x0000ffffu
#define MUSICPLAYER_STATUS_PAUSE 0x80000000u

#define FADE_VOL_SHIFT 2
#define TEMPORARY_FADE 0x0001
#define FADE_IN 0x0002

static bool sM4aInitialized;
static bool sM4aVsyncEnabled = true;
static u32 sM4aSoundMode;

static inline u8 ClampU8(u32 value) {
    return (value > 0xff) ? 0xff : (u8)value;
}

static inline u32 BuildTrackMask(u32 trackCount) {
    if (trackCount == 0) {
        return 0;
    }
    if (trackCount >= M4A_MAX_TRACKS) {
        return MUSICPLAYER_STATUS_TRACK;
    }
    return (1u << trackCount) - 1u;
}

static MusicPlayerInfo* ResolveSongPlayer(u16 songId, const SongHeader** outHeader) {
    const Song* song = &gSongTable[songId];
    u16 playerIndex = song->musicPlayerIndex;
    if (playerIndex >= M4A_PLAYER_COUNT) {
        return NULL;
    }

    const MusicPlayer* player = &gMusicPlayers[playerIndex];
    if (outHeader != NULL) {
        *outHeader = song->header;
    }
    return player->info;
}

static void ForEachTrackedPlayer(void (*cb)(MusicPlayerInfo*)) {
    u32 i;
    if (cb == NULL) {
        return;
    }
    for (i = 0; i < M4A_PLAYER_COUNT; i++) {
        MusicPlayerInfo* info = gMusicPlayers[i].info;
        if (info != NULL) {
            cb(info);
        }
    }
}

static void StopPlayer(MusicPlayerInfo* mplayInfo) {
    if (mplayInfo == NULL) {
        return;
    }
    mplayInfo->status = 0;
    mplayInfo->songHeader = NULL;
    mplayInfo->clock = 0;
}

static void ContinuePlayer(MusicPlayerInfo* mplayInfo) {
    if (mplayInfo == NULL) {
        return;
    }
    mplayInfo->status &= ~MUSICPLAYER_STATUS_PAUSE;
}

void MPlayOpen(MusicPlayerInfo* mplayInfo, MusicPlayerTrack* tracks, u8 trackCount) {
    if (mplayInfo == NULL || tracks == NULL || trackCount == 0) {
        return;
    }

    mplayInfo->tracks = tracks;
    mplayInfo->trackCount = trackCount;
    mplayInfo->songHeader = NULL;
    mplayInfo->status = 0;
    mplayInfo->clock = 0;
    mplayInfo->cmd = 0;
    mplayInfo->tempoD = 0x100;
    mplayInfo->tempoU = 0x100;
    mplayInfo->tempoI = 0x100;
    mplayInfo->tempoC = 0;
    mplayInfo->fadeOI = 0;
    mplayInfo->fadeOC = 0;
    mplayInfo->fadeOV = 0;
}

void MPlayStart(MusicPlayerInfo* mplayInfo, const SongHeader* songHeader) {
    if (mplayInfo == NULL) {
        return;
    }

    mplayInfo->songHeader = songHeader;
    mplayInfo->clock = 0;
    mplayInfo->cmd = 0;
    if (songHeader == NULL) {
        mplayInfo->status = 0;
        return;
    }

    mplayInfo->trackCount = songHeader->trackCount;
    mplayInfo->status = (mplayInfo->status & ~MUSICPLAYER_STATUS_PAUSE) | BuildTrackMask(songHeader->trackCount);
}

void MPlayStop(MusicPlayerInfo* mplayInfo) {
    StopPlayer(mplayInfo);
}

void MPlayContinue(MusicPlayerInfo* mplayInfo) {
    ContinuePlayer(mplayInfo);
}

void MPlayFadeOut(MusicPlayerInfo* mplayInfo, u16 speed) {
    if (mplayInfo == NULL) {
        return;
    }
    mplayInfo->fadeOC = speed;
    mplayInfo->fadeOI = speed;
    mplayInfo->fadeOV = (64u << FADE_VOL_SHIFT);
}

void SoundClear(void) {
    ForEachTrackedPlayer(StopPlayer);
}

void m4aSoundInit(void) {
    u32 i;
    for (i = 0; i < M4A_PLAYER_COUNT; i++) {
        MusicPlayerInfo* info = gMusicPlayers[i].info;
        MusicPlayerTrack* tracks = gMusicPlayers[i].tracks;
        u8 trackCount = gMusicPlayers[i].nTracks;
        if (info == NULL || tracks == NULL || trackCount == 0) {
            continue;
        }
        MPlayOpen(info, tracks, trackCount);
        info->unk_B = gMusicPlayers[i].unk_A;
    }
    sM4aInitialized = true;
    sM4aVsyncEnabled = true;
}

void m4aSoundMain(void) {
    u32 i;
    if (!sM4aInitialized) {
        return;
    }
    for (i = 0; i < M4A_PLAYER_COUNT; i++) {
        MusicPlayerInfo* info = gMusicPlayers[i].info;
        if (info == NULL) {
            continue;
        }
        if ((info->status & MUSICPLAYER_STATUS_TRACK) == 0) {
            continue;
        }
        if ((info->status & MUSICPLAYER_STATUS_PAUSE) != 0) {
            continue;
        }
        info->clock++;
    }
}

void m4aSoundMode(u32 mode) {
    sM4aSoundMode = mode;
    (void)sM4aSoundMode;
}

void m4aSoundVSyncOff(void) {
    sM4aVsyncEnabled = false;
}

void m4aSoundVSyncOn(void) {
    sM4aVsyncEnabled = true;
}

void m4aSoundVSync(void) {
    if (sM4aVsyncEnabled) {
        m4aSoundMain();
    }
}

void m4aSongNumStart(u16 songId) {
    const SongHeader* header = NULL;
    MusicPlayerInfo* mplayInfo = ResolveSongPlayer(songId, &header);
    MPlayStart(mplayInfo, header);
}

void m4aSongNumStartOrChange(u16 songId) {
    const SongHeader* header = NULL;
    MusicPlayerInfo* mplayInfo = ResolveSongPlayer(songId, &header);
    if (mplayInfo == NULL) {
        return;
    }
    if (mplayInfo->songHeader != header) {
        MPlayStart(mplayInfo, header);
    } else if ((mplayInfo->status & MUSICPLAYER_STATUS_TRACK) == 0 ||
               (mplayInfo->status & MUSICPLAYER_STATUS_PAUSE) != 0) {
        MPlayStart(mplayInfo, header);
    }
}

void m4aSongNumStartOrContinue(u16 songId) {
    const SongHeader* header = NULL;
    MusicPlayerInfo* mplayInfo = ResolveSongPlayer(songId, &header);
    if (mplayInfo == NULL) {
        return;
    }
    if (mplayInfo->songHeader != header || (mplayInfo->status & MUSICPLAYER_STATUS_TRACK) == 0) {
        MPlayStart(mplayInfo, header);
    } else if ((mplayInfo->status & MUSICPLAYER_STATUS_PAUSE) != 0) {
        MPlayContinue(mplayInfo);
    }
}

void m4aSongNumStop(u16 songId) {
    const SongHeader* header = NULL;
    MusicPlayerInfo* mplayInfo = ResolveSongPlayer(songId, &header);
    if (mplayInfo == NULL) {
        return;
    }
    if (mplayInfo->songHeader == header) {
        MPlayStop(mplayInfo);
    }
}

void m4aSongNumContinue(u16 songId) {
    const SongHeader* header = NULL;
    MusicPlayerInfo* mplayInfo = ResolveSongPlayer(songId, &header);
    if (mplayInfo == NULL) {
        return;
    }
    if (mplayInfo->songHeader == header) {
        MPlayContinue(mplayInfo);
    }
}

void m4aMPlayAllStop(void) {
    ForEachTrackedPlayer(StopPlayer);
}

void m4aMPlayImmInit(MusicPlayerInfo* mplayInfo) {
    u32 i;
    if (mplayInfo == NULL || mplayInfo->tracks == NULL) {
        return;
    }
    for (i = 0; i < mplayInfo->trackCount && i < M4A_MAX_TRACKS; i++) {
        MusicPlayerTrack* track = &mplayInfo->tracks[i];
        if (track->flags & MPT_FLG_EXIST) {
            memset(track, 0, sizeof(*track));
            track->flags = MPT_FLG_EXIST;
            track->bendRange = 2;
            track->volX = 64;
            track->lfoSpeed = 22;
            track->tone.type = 1;
        }
    }
}

void m4aMPlayTempoControl(MusicPlayerInfo* mplayInfo, u16 tempo) {
    if (mplayInfo == NULL) {
        return;
    }
    mplayInfo->tempoD = tempo;
    mplayInfo->tempoU = tempo;
}

void m4aMPlayVolumeControl(MusicPlayerInfo* mplayInfo, u16 trackBits, u16 volume) {
    u32 i;
    if (mplayInfo == NULL || mplayInfo->tracks == NULL) {
        return;
    }
    for (i = 0; i < mplayInfo->trackCount && i < M4A_MAX_TRACKS; i++) {
        if (((trackBits >> i) & 1u) == 0) {
            continue;
        }
        mplayInfo->tracks[i].volX = ClampU8(volume);
        mplayInfo->tracks[i].flags |= MPT_FLG_VOLCHG;
    }
}

void m4aMPlayPitchControl(MusicPlayerInfo* mplayInfo, u16 trackBits, s16 pitch) {
    u32 i;
    if (mplayInfo == NULL || mplayInfo->tracks == NULL) {
        return;
    }
    for (i = 0; i < mplayInfo->trackCount && i < M4A_MAX_TRACKS; i++) {
        if (((trackBits >> i) & 1u) == 0) {
            continue;
        }
        mplayInfo->tracks[i].pitM = (u8)pitch;
        mplayInfo->tracks[i].flags |= MPT_FLG_PITCHG;
    }
}

void m4aMPlayPanpotControl(MusicPlayerInfo* mplayInfo, u16 trackBits, s8 pan) {
    u32 i;
    if (mplayInfo == NULL || mplayInfo->tracks == NULL) {
        return;
    }
    for (i = 0; i < mplayInfo->trackCount && i < M4A_MAX_TRACKS; i++) {
        if (((trackBits >> i) & 1u) == 0) {
            continue;
        }
        mplayInfo->tracks[i].pan = pan;
        mplayInfo->tracks[i].flags |= MPT_FLG_VOLCHG;
    }
}

void m4aMPlayModDepthSet(MusicPlayerInfo* mplayInfo, u16 trackBits, u8 modDepth) {
    u32 i;
    if (mplayInfo == NULL || mplayInfo->tracks == NULL) {
        return;
    }
    for (i = 0; i < mplayInfo->trackCount && i < M4A_MAX_TRACKS; i++) {
        if (((trackBits >> i) & 1u) == 0) {
            continue;
        }
        mplayInfo->tracks[i].mod = modDepth;
    }
}

void m4aMPlayLFOSpeedSet(MusicPlayerInfo* mplayInfo, u16 trackBits, u8 lfoSpeed) {
    u32 i;
    if (mplayInfo == NULL || mplayInfo->tracks == NULL) {
        return;
    }
    for (i = 0; i < mplayInfo->trackCount && i < M4A_MAX_TRACKS; i++) {
        if (((trackBits >> i) & 1u) == 0) {
            continue;
        }
        mplayInfo->tracks[i].lfoSpeed = lfoSpeed;
        mplayInfo->tracks[i].lfoSpeedC = lfoSpeed;
    }
}

void m4aMPlayContinue(MusicPlayerInfo* mplayInfo) {
    MPlayContinue(mplayInfo);
}

void m4aMPlayAllContinue(void) {
    ForEachTrackedPlayer(ContinuePlayer);
}

void m4aMPlayFadeOut(MusicPlayerInfo* mplayInfo, u16 speed) {
    MPlayFadeOut(mplayInfo, speed);
}

void m4aMPlayFadeOutTemporarily(MusicPlayerInfo* mplayInfo, u16 speed) {
    if (mplayInfo == NULL) {
        return;
    }
    mplayInfo->fadeOC = speed;
    mplayInfo->fadeOI = speed;
    mplayInfo->fadeOV = (64u << FADE_VOL_SHIFT) | TEMPORARY_FADE;
}

void m4aMPlayFadeIn(MusicPlayerInfo* mplayInfo, u16 speed) {
    if (mplayInfo == NULL) {
        return;
    }
    mplayInfo->fadeOC = speed;
    mplayInfo->fadeOI = speed;
    mplayInfo->fadeOV = (0u << FADE_VOL_SHIFT) | FADE_IN;
    mplayInfo->status &= ~MUSICPLAYER_STATUS_PAUSE;
}
