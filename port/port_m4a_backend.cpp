#include "port_m4a_backend.h"
#include "port_config.h"
#include "sound.h"

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#ifdef Stop
#undef Stop
#endif

#include <filesystem>
#include <optional>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <climits>
#include <mach-o/dyld.h>
#else
#include <climits>
#include <unistd.h>
#endif

extern "C" {
typedef struct SongHeader SongHeader;
typedef struct MusicPlayerInfo MusicPlayerInfo;
typedef struct MusicPlayerTrack MusicPlayerTrack;
typedef struct MusicPlayer {
    MusicPlayerInfo* info;
    MusicPlayerTrack* tracks;
    uint8_t nTracks;
    uint16_t unk_A;
} MusicPlayer;

extern const MusicPlayer gMusicPlayers[];
}

#ifdef PACKED
#undef PACKED
#endif

#include "AgbTypes.hpp"
#include "MP2KContext.hpp"
#include "Rom.hpp"
#include "Types.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kPlayerCount = 32;
constexpr uint32_t kMaxTracks = 16;
constexpr uint32_t kSongCount = SFX_221 + 1;

struct BackendState {
    bool initialized = false;
    bool vsyncEnabled = true;
    uint32_t sampleRate = 48000;
    uint32_t soundMode = 0;
    bool songMapLoaded = false;
    std::unique_ptr<Rom> rom;
    std::unique_ptr<MP2KContext> ctx;
    std::vector<int16_t> pendingSamples;
    size_t pendingFrameOffset = 0;
    std::array<size_t, kSongCount> songHeaderOffsets;
    uint8_t trackVolumes[kPlayerCount][kMaxTracks];
    int8_t trackPans[kPlayerCount][kMaxTracks];
};

BackendState sState;
std::mutex sStateMutex;

static MP2KSoundMode MakeSoundMode(void) {
    MP2KSoundMode mode;
    mode.vol = 0x0f;
    mode.rev = 0x80;
    mode.freq = 0x05;
    mode.maxChannels = 0x08;
    mode.dacConfig = 0x09;
    return mode;
}

static AgbplaySoundMode MakeAgbplayMode(void) {
    AgbplaySoundMode mode;
    mode.resamplerTypeNormal = ResamplerType::LINEAR;
    mode.resamplerTypeFixed = ResamplerType::LINEAR;
    mode.reverbType = ReverbType::NORMAL;
    mode.reverbForce = 0;
    mode.cgbPolyphony = CGBPolyphony::MONO_STRICT;
    mode.dmaBufferLen = 0x630;
    mode.accurateCh3Quantization = true;
    mode.accurateCh3Volume = true;
    mode.emulateCgbSustainBug = true;
    return mode;
}

static size_t SongHeaderToRomPos(const SongHeader* songHeader) {
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(songHeader);

    if (songHeader == nullptr || gRomData == nullptr) {
        return 0;
    }
    if (ptr < gRomData || ptr >= gRomData + gRomSize) {
        return 0;
    }
    return static_cast<size_t>(ptr - gRomData);
}

static const char* GetCurrentVariantName(void) {
    switch (gRomRegion) {
        case ROM_REGION_EU:
            return "EU";
        case ROM_REGION_USA:
        default:
            return "USA";
    }
}

static std::string LoadTextFile(const char* path) {
    std::ifstream stream(path, std::ios::binary);

    if (!stream.good()) {
        return {};
    }

    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

static std::optional<std::filesystem::path> ExeDirForSounds() {
#ifdef _WIN32
    std::wstring buf(MAX_PATH, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (len == 0) return std::nullopt;
    while (len >= buf.size() - 1) {
        buf.resize(buf.size() * 2);
        len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len == 0) return std::nullopt;
    }
    buf.resize(len);
    return std::filesystem::path(buf).parent_path();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) == 0) {
        std::error_code ec;
        std::filesystem::path canonical = std::filesystem::weakly_canonical(buf.c_str(), ec);
        if (!ec) return canonical.parent_path();
    }
    return std::nullopt;
#else
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf));
    if (len > 0 && static_cast<size_t>(len) < sizeof(buf)) {
        return std::filesystem::path(std::string(buf, static_cast<size_t>(len))).parent_path();
    }
    return std::nullopt;
#endif
}

static std::string LoadSoundsJson(void) {
    /* Search beside the binary first (release-tarball layout), then walk a
     * couple of cwd-relative dev locations. Mirrors the asset loader's
     * lookup pattern so the release zip's sounds.json is found regardless
     * of the user's current directory. */
    std::vector<std::string> paths;
    if (auto dir = ExeDirForSounds(); dir.has_value()) {
        paths.push_back((*dir / "sounds.json").string());
        paths.push_back((*dir / "assets" / "sounds.json").string());
    }
    paths.push_back("assets/sounds.json");
    paths.push_back("../assets/sounds.json");
    paths.push_back("../../assets/sounds.json");

    for (const std::string& path : paths) {
        std::string text = LoadTextFile(path.c_str());
        if (!text.empty()) {
            std::fprintf(stderr, "[AUDIO] loaded %s\n", path.c_str());
            return text;
        }
    }

    std::fprintf(stderr, "[AUDIO] sounds.json not found — songs will be silent\n");
    return {};
}

static bool ParseIntAfterKey(const std::string& text, size_t keyPos, long long& outValue) {
    size_t pos = text.find(':', keyPos);
    size_t end = 0;

    if (pos == std::string::npos) {
        return false;
    }

    pos++;
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        pos++;
    }

    outValue = std::strtoll(text.c_str() + pos, nullptr, 10);
    end = pos;
    if (end >= text.size() || (!std::isdigit(static_cast<unsigned char>(text[end])) && text[end] != '-')) {
        return false;
    }

    return true;
}

static size_t FindObjectEnd(const std::string& text, size_t objectStart) {
    int depth = 0;
    bool inString = false;
    bool escaped = false;

    for (size_t i = objectStart; i < text.size(); i++) {
        char c = text[i];

        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }

        if (c == '"') {
            inString = true;
            continue;
        }
        if (c == '{') {
            depth++;
            continue;
        }
        if (c == '}') {
            depth--;
            if (depth == 0) {
                return i;
            }
        }
    }

    return std::string::npos;
}

static bool ObjectMatchesVariant(const std::string& objectText, const char* variantName) {
    size_t variantsPos = objectText.find("\"variants\"");

    if (variantsPos == std::string::npos) {
        return true;
    }

    return objectText.find(std::string("\"") + variantName + "\"", variantsPos) != std::string::npos;
}

static void LoadSongMapLocked(void) {
    std::string jsonText;
    const char* variantName = GetCurrentVariantName();
    long long variantOffset = 0;
    size_t searchPos = 0;

    sState.songHeaderOffsets.fill(0);
    sState.songMapLoaded = true;

    jsonText = LoadSoundsJson();
    if (jsonText.empty()) {
        return;
    }

    {
        size_t offsetsPos = jsonText.find("\"offsets\"");
        if (offsetsPos != std::string::npos) {
            size_t variantPos = jsonText.find(std::string("\"") + variantName + "\"", offsetsPos);
            if (variantPos != std::string::npos) {
                ParseIntAfterKey(jsonText, variantPos, variantOffset);
            }
        }
    }

    while (true) {
        size_t pathPos = jsonText.find("\"path\": \"sounds/", searchPos);
        size_t labelStart;
        size_t labelEnd;
        size_t objectStart;
        size_t objectEnd;
        std::string objectText;
        std::string label;
        long long startOffset = -1;
        long long headerOffset = -1;

        if (pathPos == std::string::npos) {
            break;
        }

        labelStart = pathPos + strlen("\"path\": \"sounds/");
        labelEnd = jsonText.find(".bin\"", labelStart);
        objectStart = jsonText.rfind('{', pathPos);
        if (labelEnd == std::string::npos || objectStart == std::string::npos) {
            break;
        }

        objectEnd = FindObjectEnd(jsonText, objectStart);
        if (objectEnd == std::string::npos) {
            break;
        }

        objectText = jsonText.substr(objectStart, objectEnd - objectStart + 1);
        label = jsonText.substr(labelStart, labelEnd - labelStart);
        searchPos = objectEnd + 1;

        if (!ObjectMatchesVariant(objectText, variantName)) {
            continue;
        }

        {
            size_t startsPos = objectText.find("\"starts\"");
            if (startsPos != std::string::npos) {
                size_t variantPos = objectText.find(std::string("\"") + variantName + "\"", startsPos);
                if (variantPos != std::string::npos) {
                    ParseIntAfterKey(objectText, variantPos, startOffset);
                }
            } else {
                size_t startPos = objectText.find("\"start\"");
                if (startPos != std::string::npos) {
                    if (ParseIntAfterKey(objectText, startPos, startOffset)) {
                        startOffset += variantOffset;
                    }
                }
            }
        }

        {
            size_t headerPos = objectText.find("\"headerOffset\"");
            if (headerPos != std::string::npos) {
                ParseIntAfterKey(objectText, headerPos, headerOffset);
            }
        }

        if (startOffset < 0 || headerOffset < 0) {
            continue;
        }

        for (uint32_t i = 0; i < kSongCount; i++) {
            const char* songLabel = Port_GetSongLabel((uint16_t)i);

            if (songLabel != nullptr && label == songLabel) {
                sState.songHeaderOffsets[i] = static_cast<size_t>(startOffset + headerOffset);
                break;
            }
        }
    }
}

static size_t SongIdToRomPosLocked(uint16_t songId) {
    if (!sState.songMapLoaded) {
        LoadSongMapLocked();
    }

    if (songId >= kSongCount) {
        return 0;
    }

    return sState.songHeaderOffsets[songId];
}

static void ResetTrackMixControlsLocked(void) {
    for (uint32_t i = 0; i < kPlayerCount; i++) {
        for (uint32_t j = 0; j < kMaxTracks; j++) {
            sState.trackVolumes[i][j] = 0xff;
            sState.trackPans[i][j] = 0;
        }
    }
}

static PlayerTableInfo BuildPlayerTable(void) {
    PlayerTableInfo playerTable;
    playerTable.reserve(kPlayerCount);

    for (uint32_t i = 0; i < kPlayerCount; i++) {
        PlayerInfo info;
        info.maxTracks = gMusicPlayers[i].nTracks;
        info.usePriority = gMusicPlayers[i].unk_A != 0;
        playerTable.push_back(info);
    }

    return playerTable;
}

static void RebuildContextLocked(void) {
    std::span<uint8_t> romSpan;
    SongTableInfo songTableInfo;

    sState.pendingSamples.clear();
    sState.pendingFrameOffset = 0;
    sState.ctx.reset();
    sState.rom.reset();
    sState.songMapLoaded = false;
    sState.songHeaderOffsets.fill(0);

    if (gRomData == nullptr || gRomSize == 0 || !sState.initialized) {
        return;
    }

    romSpan = std::span<uint8_t>(gRomData, gRomSize);
    sState.rom = std::make_unique<Rom>(Rom::LoadFromBufferRef(romSpan));

    songTableInfo.pos = SongTableInfo::POS_AUTO;
    songTableInfo.count = 0;
    songTableInfo.tableIdx = 0;

    sState.ctx = std::make_unique<MP2KContext>(
        sState.sampleRate, -1, *sState.rom, MakeSoundMode(), MakeAgbplayMode(), songTableInfo, BuildPlayerTable()
    );
    sState.ctx->m4aSoundMode(sState.soundMode);
}

static bool HasActivePlaybackLocked(void) {
    if (!sState.ctx) {
        return false;
    }

    for (const auto& player : sState.ctx->players) {
        if (player.playing || !player.finished) {
            return true;
        }
    }

    return !sState.ctx->sndChannels.empty() || !sState.ctx->sq1Channels.empty() || !sState.ctx->sq2Channels.empty() ||
           !sState.ctx->waveChannels.empty() || !sState.ctx->noiseChannels.empty();
}

static void RenderChunkLocked(void) {
    const size_t sampleCount = sState.ctx->mixer.GetSamplesPerBuffer();

    sState.pendingSamples.assign(sampleCount * 2, 0);
    sState.pendingFrameOffset = 0;

    if (!sState.vsyncEnabled || !HasActivePlaybackLocked()) {
        return;
    }

    sState.ctx->m4aSoundMain();

    for (size_t sampleIndex = 0; sampleIndex < sampleCount; sampleIndex++) {
        float left = 0.0f;
        float right = 0.0f;

        for (uint32_t playerIndex = 0; playerIndex < std::min<uint32_t>(kPlayerCount, sState.ctx->players.size());
             playerIndex++) {
            const auto& player = sState.ctx->players[playerIndex];

            for (size_t trackIndex = 0; trackIndex < std::min<size_t>(kMaxTracks, player.tracks.size()); trackIndex++) {
                const auto& track = player.tracks[trackIndex];
                const float gain = static_cast<float>(sState.trackVolumes[playerIndex][trackIndex]) / 255.0f;
                const float pan = static_cast<float>(sState.trackPans[playerIndex][trackIndex]) / 64.0f;
                float trackLeft;
                float trackRight;

                if (track.muted || track.audioBuffer.size() <= sampleIndex || gain <= 0.0f) {
                    continue;
                }

                trackLeft = track.audioBuffer[sampleIndex].left * gain;
                trackRight = track.audioBuffer[sampleIndex].right * gain;

                if (pan > 0.0f) {
                    trackLeft *= (1.0f - std::min(pan, 1.0f));
                } else if (pan < 0.0f) {
                    trackRight *= (1.0f - std::min(-pan, 1.0f));
                }

                left += trackLeft;
                right += trackRight;
            }
        }

        left = std::clamp(left, -1.0f, 1.0f);
        right = std::clamp(right, -1.0f, 1.0f);

        sState.pendingSamples[sampleIndex * 2 + 0] = static_cast<int16_t>(std::lround(left * 32767.0f));
        sState.pendingSamples[sampleIndex * 2 + 1] = static_cast<int16_t>(std::lround(right * 32767.0f));
    }
}

} // namespace

bool Port_M4A_Backend_Init(uint32_t sampleRate) {
    std::lock_guard<std::mutex> lock(sStateMutex);

    sState.initialized = true;
    sState.sampleRate = sampleRate;
    sState.soundMode = 0;
    sState.vsyncEnabled = true;
    ResetTrackMixControlsLocked();
    return true;
}

void Port_M4A_Backend_Shutdown(void) {
    std::lock_guard<std::mutex> lock(sStateMutex);

    sState.pendingSamples.clear();
    sState.pendingFrameOffset = 0;
    sState.ctx.reset();
    sState.rom.reset();
    sState.initialized = false;
}

void Port_M4A_Backend_Reset(void) {
    std::lock_guard<std::mutex> lock(sStateMutex);

    ResetTrackMixControlsLocked();
    RebuildContextLocked();
}

void Port_M4A_Backend_SoundInit(uint32_t soundMode) {
    std::lock_guard<std::mutex> lock(sStateMutex);

    sState.soundMode = soundMode;
    sState.vsyncEnabled = true;
    ResetTrackMixControlsLocked();
    RebuildContextLocked();
}

void Port_M4A_Backend_SetSoundMode(uint32_t soundMode) {
    std::lock_guard<std::mutex> lock(sStateMutex);

    sState.soundMode = soundMode;
    if (sState.ctx) {
        sState.ctx->m4aSoundMode(soundMode);
    }
}

void Port_M4A_Backend_SetVSyncEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(sStateMutex);
    sState.vsyncEnabled = enabled;
}

bool Port_M4A_Backend_StartSongById(uint8_t playerIndex, uint16_t songId) {
    std::lock_guard<std::mutex> lock(sStateMutex);
    const size_t songPos = SongIdToRomPosLocked(songId);

    if (!sState.ctx || playerIndex >= sState.ctx->players.size()) {
        return false;
    }
    if (songPos == 0 || songPos >= gRomSize) {
        sState.ctx->m4aMPlayStop(playerIndex);
        return false;
    }

    sState.ctx->m4aMPlayStart(playerIndex, songPos);
    return true;
}

void Port_M4A_Backend_StartSong(uint8_t playerIndex, const SongHeader* songHeader) {
    std::lock_guard<std::mutex> lock(sStateMutex);
    const size_t songPos = SongHeaderToRomPos(songHeader);

    if (!sState.ctx || playerIndex >= sState.ctx->players.size()) {
        return;
    }

    if (songPos == 0) {
        sState.ctx->m4aMPlayStop(playerIndex);
        return;
    }

    sState.ctx->m4aMPlayStart(playerIndex, songPos);
}

void Port_M4A_Backend_StopPlayer(uint8_t playerIndex) {
    std::lock_guard<std::mutex> lock(sStateMutex);

    if (!sState.ctx || playerIndex >= sState.ctx->players.size()) {
        return;
    }

    sState.ctx->m4aMPlayStop(playerIndex);
}

void Port_M4A_Backend_ContinuePlayer(uint8_t playerIndex) {
    std::lock_guard<std::mutex> lock(sStateMutex);

    if (!sState.ctx || playerIndex >= sState.ctx->players.size()) {
        return;
    }

    sState.ctx->m4aMPlayContinue(playerIndex);
}

void Port_M4A_Backend_SetTrackVolume(uint8_t playerIndex, uint16_t trackBits, uint16_t volume) {
    std::lock_guard<std::mutex> lock(sStateMutex);
    const uint8_t clamped = volume > 0xff ? 0xff : static_cast<uint8_t>(volume);

    if (playerIndex >= kPlayerCount) {
        return;
    }

    for (uint32_t trackIndex = 0; trackIndex < kMaxTracks; trackIndex++) {
        if (((trackBits >> trackIndex) & 1u) != 0) {
            sState.trackVolumes[playerIndex][trackIndex] = clamped;
        }
    }
}

void Port_M4A_Backend_SetTrackPan(uint8_t playerIndex, uint16_t trackBits, int8_t pan) {
    std::lock_guard<std::mutex> lock(sStateMutex);

    if (playerIndex >= kPlayerCount) {
        return;
    }

    for (uint32_t trackIndex = 0; trackIndex < kMaxTracks; trackIndex++) {
        if (((trackBits >> trackIndex) & 1u) != 0) {
            sState.trackPans[playerIndex][trackIndex] = pan;
        }
    }
}

void Port_M4A_Backend_Render(int16_t* outSamples, uint32_t frameCount, bool mute) {
    std::lock_guard<std::mutex> lock(sStateMutex);
    uint32_t framesRemaining = frameCount;

    if (outSamples == nullptr) {
        return;
    }

    while (framesRemaining > 0) {
        size_t availableFrames;
        size_t copyFrames;

        if (!sState.ctx) {
            memset(outSamples, 0, sizeof(int16_t) * frameCount * 2);
            return;
        }

        if (sState.pendingFrameOffset >= sState.pendingSamples.size() / 2) {
            RenderChunkLocked();
        }

        availableFrames = (sState.pendingSamples.size() / 2) - sState.pendingFrameOffset;
        if (availableFrames == 0) {
            memset(outSamples, 0, sizeof(int16_t) * framesRemaining * 2);
            return;
        }

        copyFrames = std::min<size_t>(availableFrames, framesRemaining);
        if (mute) {
            memset(outSamples, 0, sizeof(int16_t) * copyFrames * 2);
        } else {
            memcpy(
                outSamples,
                &sState.pendingSamples[sState.pendingFrameOffset * 2],
                sizeof(int16_t) * copyFrames * 2
            );
        }

        outSamples += copyFrames * 2;
        framesRemaining -= static_cast<uint32_t>(copyFrames);
        sState.pendingFrameOffset += copyFrames;
    }
}
