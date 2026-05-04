#include "port_asset_loader.h"
#include "port_asset_pipeline.hpp"

extern "C" {
#define this this_
#include "common.h"
#include "port_gba_mem.h"
#include "port_rom.h"
#include "port_asset_index.h"
#include "structures.h"
#include "area.h"
#undef this

extern RoomHeader* gAreaRoomHeaders[];
extern void* gAreaRoomMaps[];
extern void* gAreaTable[];
extern void* gAreaTileSets[];
extern void* gAreaTiles[];
extern u32* gTranslations[];
extern SpritePtr gSpritePtrs[];
extern u16* gMoreSpritePtrs[];
extern Frame* gSpriteAnimations_322[];
}

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

#include <nlohmann/json.hpp>

#include <array>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <climits>
#include <mach-o/dyld.h>
#else
#include <climits>
#include <unistd.h>
#endif

namespace {

struct SaveHeaderLite {
    int signature;
    u8 saveFileId;
    u8 msgSpeed;
    u8 brightness;
    u8 language;
};

struct GfxGroupEntryData {
    u8 unknown;
    u32 dest;
    std::string file;
    bool terminator;
};

struct PaletteFileRefData {
    std::string file;
    u32 byteOffset;
    u32 size;
    u32 numPalettes;
};

struct PaletteGroupEntryData {
    u8 destPaletteNum;
    u8 numPalettes;
    bool terminator;
    std::vector<PaletteFileRefData> paletteFiles;
};

struct MapDefinitionRefData {
    bool multiple = false;
    bool compressed = false;
    bool isPaletteGroup = false;
    u16 paletteGroup = 0;
    u32 dest = 0;
    u32 size = 0;
    std::string file;
};

struct AreaPropertyEntryData {
    std::vector<std::string> files;
};

struct SpritePtrEntryData {
    std::vector<std::string> animations;
    std::string framesFile;
    std::string ptrFile;
    u32 pad = 0;
};

constexpr size_t kAreaCount = 0x90;
constexpr size_t kSpritePtrMax = 512;
constexpr size_t kSpriteAnim322Count = 128;

struct AssetGroupCache {
    bool initAttempted = false;
    bool ready = false;
    bool spritePtrsLoaded = false;
    bool areaTablesLoaded = false;
    bool textsLoaded = false;
    bool hasSpritePtrData = false;
    bool hasAreaData = false;
    bool hasTextData = false;
    std::filesystem::path assetsRoot;
    std::unordered_map<u32, std::vector<GfxGroupEntryData>> gfxGroups;
    std::unordered_map<u32, std::vector<PaletteGroupEntryData>> paletteGroups;
    std::array<std::vector<RoomHeader>, kAreaCount> areaRoomHeaders;
    std::array<std::vector<std::vector<MapDefinitionRefData>>, kAreaCount> areaTileSets;
    std::array<std::vector<std::vector<MapDefinitionRefData>>, kAreaCount> areaRoomMaps;
    std::array<std::vector<MapDefinitionRefData>, kAreaCount> areaTiles;
    std::array<std::vector<AreaPropertyEntryData>, kAreaCount> areaTables;
    std::vector<SpritePtrEntryData> spritePtrs;
    std::unordered_map<std::string, std::unique_ptr<std::vector<u8>>> binaryFiles;
    std::unordered_map<std::string, u32> mapAssetFileToIndex;
    std::vector<std::string> mapAssetFiles;
    std::array<std::vector<MapDataDefinition*>, kAreaCount> areaTileSetPtrs;
    std::array<std::vector<MapDataDefinition*>, kAreaCount> areaRoomMapPtrs;
    std::array<std::vector<void**>, kAreaCount> areaTablePtrs;
    std::array<MapDataDefinition*, kAreaCount> areaTilesPtrs = {};
    std::array<std::vector<std::unique_ptr<void*[]>>, kAreaCount> areaPropertyStorage;
    std::array<std::vector<std::unique_ptr<MapDataDefinition[]>>, kAreaCount> mapDefStorage;
    std::vector<std::vector<const u8*>> spriteAnimationPtrs;
    /* Padded copies of animation .bin files that ended on a loop frame
     * without a trailing loop_back byte. On GBA the byte happens to be the
     * first byte of the next animation in ROM; the extracted .bins truncate
     * at exact ROM size. We rebuild the missing trailing byte here. */
    std::vector<std::unique_ptr<std::vector<u8>>> animPaddedBuffers;
    std::array<std::vector<u8>, 7> translationBuffers;
    std::array<std::unordered_map<u32, std::string>, 7> textFilesById;
};

AssetGroupCache gAssetGroupCache;
std::unordered_set<std::string> gAssetLogOnceKeys;

std::string PathForLog(const std::filesystem::path& path) {
    return path.generic_string();
}

void AssetLogOnce(const std::string& key, const char* fmt, ...) {
    if (!gAssetLogOnceKeys.insert(key).second) {
        return;
    }

    std::va_list args;
    va_start(args, fmt);
    std::fprintf(stderr, "[ASSET] ");
    std::vfprintf(stderr, fmt, args);
    std::fprintf(stderr, "\n");
    va_end(args);
}

// Returns the directory containing the running executable, or — only as a
// last resort — std::filesystem::current_path(). The release tarball ships
// `tmc_pc` and `assets[_src]/` as siblings, so the exe directory is the
// authoritative answer; cwd just happens to coincide with it when launched
// from a terminal in the same dir.
std::optional<std::filesystem::path> GetExecutableDirectory() {
#ifdef _WIN32
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (len == 0) {
        return std::nullopt;
    }
    while (len >= buffer.size() - 1) {
        buffer.resize(buffer.size() * 2);
        len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (len == 0) {
            return std::nullopt;
        }
    }
    buffer.resize(len);
    return std::filesystem::path(buffer).parent_path();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        std::error_code ec;
        std::filesystem::path canonical = std::filesystem::weakly_canonical(buffer.c_str(), ec);
        if (!ec) {
            return canonical.parent_path();
        }
    }
    std::error_code ec;
    return std::filesystem::current_path(ec);
#else
    char buffer[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer));
    if (len > 0 && static_cast<size_t>(len) < sizeof(buffer)) {
        return std::filesystem::path(std::string(buffer, static_cast<size_t>(len))).parent_path();
    }
    std::error_code ec;
    return std::filesystem::current_path(ec);
#endif
}

/* Build the search list once: exe-dir first (typical install layout), then
 * cwd (works around users who launch via a custom dynamic loader, e.g.
 * `$HOME/glibc/ld-linux.so.2 ./tmc_pc`, in which case /proc/self/exe points
 * at the loader rather than tmc_pc — issue #2). Caller filters by which
 * candidate actually contains the expected JSON manifest. */
static std::vector<std::filesystem::path> AssetSearchRoots() {
    std::vector<std::filesystem::path> roots;
    const auto exeDir = GetExecutableDirectory();
    if (exeDir.has_value()) {
        roots.push_back(*exeDir);
    }
    std::error_code ec;
    const auto cwd = std::filesystem::current_path(ec);
    if (!ec && (!exeDir.has_value() || *exeDir != cwd)) {
        roots.push_back(cwd);
    }
    return roots;
}

std::optional<std::filesystem::path> FindEditableAssetsRoot() {
    for (const auto& root : AssetSearchRoots()) {
        const std::filesystem::path candidate = root / "assets_src";
        if (std::filesystem::exists(candidate / "gfx_groups.json") &&
            std::filesystem::exists(candidate / "palette_groups.json") &&
            std::filesystem::exists(candidate / "palettes.json")) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> FindRuntimeAssetsRoot() {
    for (const auto& root : AssetSearchRoots()) {
        const std::filesystem::path candidate = root / "assets";
        if (std::filesystem::exists(candidate / "gfx_groups.json") &&
            std::filesystem::exists(candidate / "palette_groups.json")) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::filesystem::path RuntimeRootForEditableRoot(const std::filesystem::path& editableRoot) {
    return editableRoot.parent_path() / "assets";
}

bool LoadJsonFile(const std::filesystem::path& path, nlohmann::json& outJson) {
    std::ifstream input(path);
    if (!input.good()) {
        return false;
    }

    input >> outJson;
    return true;
}

std::string JsonStringOrEmpty(const nlohmann::json& object, const char* key) {
    if (!object.contains(key) || object[key].is_null() || !object[key].is_string()) {
        return {};
    }
    return object[key].get<std::string>();
}

bool IsRomPointer(const void* ptr, size_t size = 1) {
    if (ptr == nullptr || gRomData == nullptr || gRomSize < size) {
        return false;
    }

    const uintptr_t start = reinterpret_cast<uintptr_t>(gRomData);
    const uintptr_t end = start + static_cast<uintptr_t>(gRomSize);
    const uintptr_t at = reinterpret_cast<uintptr_t>(ptr);
    return at >= start && at <= end - size;
}

bool LoadOptionalJson(const std::filesystem::path& path, nlohmann::json& json) {
    if (!std::filesystem::exists(path)) {
        json = nlohmann::json();
        return true;
    }
    return LoadJsonFile(path, json);
}

const std::vector<u8>* LoadBinaryFileCached(const std::string& relativePath);

void ParseGfxGroups(const nlohmann::json& root) {
    gAssetGroupCache.gfxGroups.clear();

    for (auto it = root.begin(); it != root.end(); ++it) {
        const u32 group = static_cast<u32>(std::stoul(it.key()));
        std::vector<GfxGroupEntryData> entries;

        for (const auto& jsonEntry : it.value()) {
            GfxGroupEntryData entry = {};
            entry.unknown = static_cast<u8>(jsonEntry.value("unknown", 0));
            entry.dest = jsonEntry.value("dest", 0u);
            entry.file = JsonStringOrEmpty(jsonEntry, "file");
            entry.terminator = jsonEntry.value("terminator", false);
            entries.push_back(std::move(entry));
        }

        gAssetGroupCache.gfxGroups.emplace(group, std::move(entries));
    }
}

void ParsePaletteGroups(const nlohmann::json& root) {
    gAssetGroupCache.paletteGroups.clear();

    for (auto it = root.begin(); it != root.end(); ++it) {
        const u32 group = static_cast<u32>(std::stoul(it.key()));
        std::vector<PaletteGroupEntryData> entries;

        if (!it.value().contains("entries") || !it.value()["entries"].is_array()) {
            continue;
        }

        for (const auto& jsonEntry : it.value()["entries"]) {
            PaletteGroupEntryData entry = {};
            entry.destPaletteNum = static_cast<u8>(jsonEntry.value("dest_palette_num", 0));
            entry.numPalettes = static_cast<u8>(jsonEntry.value("num_palettes", 0));
            entry.terminator = jsonEntry.value("terminator", false);

            if (jsonEntry.contains("palette_files") && jsonEntry["palette_files"].is_array()) {
                for (const auto& jsonRef : jsonEntry["palette_files"]) {
                    PaletteFileRefData ref = {};
                    ref.file = JsonStringOrEmpty(jsonRef, "file");
                    ref.byteOffset = jsonRef.value("byte_offset", 0u);
                    ref.size = jsonRef.value("size", 0u);
                    ref.numPalettes = jsonRef.value("num_palettes", 0u);
                    entry.paletteFiles.push_back(std::move(ref));
                }
            }

            entries.push_back(std::move(entry));
        }

        gAssetGroupCache.paletteGroups.emplace(group, std::move(entries));
    }
}

std::vector<MapDefinitionRefData> ParseMapDefinitionList(const nlohmann::json& root) {
    std::vector<MapDefinitionRefData> refs;

    if (!root.is_array()) {
        return refs;
    }

    for (const auto& jsonEntry : root) {
        MapDefinitionRefData ref = {};
        ref.multiple = jsonEntry.value("multiple", false);
        ref.compressed = jsonEntry.value("compressed", false);

        if (jsonEntry.contains("palette_group") && jsonEntry["palette_group"].is_number_unsigned()) {
            ref.isPaletteGroup = true;
            ref.paletteGroup = static_cast<u16>(jsonEntry["palette_group"].get<u32>());
        } else {
            ref.dest = jsonEntry.value("dest", 0u);
            ref.size = jsonEntry.value("size", 0u);
            ref.file = JsonStringOrEmpty(jsonEntry, "file");
        }

        refs.push_back(std::move(ref));
    }

    return refs;
}

void ParseAreaRoomHeaders(const nlohmann::json& root) {
    for (auto& areaRooms : gAssetGroupCache.areaRoomHeaders) {
        areaRooms.clear();
    }

    if (!root.is_object()) {
        return;
    }

    for (auto it = root.begin(); it != root.end(); ++it) {
        const u32 area = static_cast<u32>(std::stoul(it.key()));
        if (area >= kAreaCount || !it.value().is_array()) {
            continue;
        }

        auto& out = gAssetGroupCache.areaRoomHeaders[area];
        for (const auto& jsonRoom : it.value()) {
            RoomHeader header = {};
            header.map_x = static_cast<u16>(jsonRoom.value("map_x", 0));
            header.map_y = static_cast<u16>(jsonRoom.value("map_y", 0));
            header.pixel_width = static_cast<u16>(jsonRoom.value("pixel_width", 0));
            header.pixel_height = static_cast<u16>(jsonRoom.value("pixel_height", 0));
            header.tileSet_id = static_cast<u16>(jsonRoom.value("tile_set_id", 0xFFFF));
            out.push_back(header);
        }

        RoomHeader terminator = {};
        terminator.map_x = 0xFFFF;
        out.push_back(terminator);
    }
}

void ParseAreaMapTable(const nlohmann::json& root,
                       std::array<std::vector<std::vector<MapDefinitionRefData>>, kAreaCount>& outTable) {
    for (auto& areaEntries : outTable) {
        areaEntries.clear();
    }

    if (!root.is_object()) {
        return;
    }

    for (auto it = root.begin(); it != root.end(); ++it) {
        const u32 area = static_cast<u32>(std::stoul(it.key()));
        if (area >= kAreaCount || !it.value().is_array()) {
            continue;
        }

        auto& out = outTable[area];
        for (const auto& jsonSequence : it.value()) {
            out.push_back(ParseMapDefinitionList(jsonSequence));
        }
    }
}

void ParseAreaTiles(const nlohmann::json& root) {
    for (auto& areaEntries : gAssetGroupCache.areaTiles) {
        areaEntries.clear();
    }

    if (!root.is_object()) {
        return;
    }

    for (auto it = root.begin(); it != root.end(); ++it) {
        const u32 area = static_cast<u32>(std::stoul(it.key()));
        if (area >= kAreaCount) {
            continue;
        }

        gAssetGroupCache.areaTiles[area] = ParseMapDefinitionList(it.value());
    }
}

void ParseAreaTables(const nlohmann::json& root) {
    for (auto& areaEntries : gAssetGroupCache.areaTables) {
        areaEntries.clear();
    }

    if (!root.is_object()) {
        return;
    }

    for (auto it = root.begin(); it != root.end(); ++it) {
        const u32 area = static_cast<u32>(std::stoul(it.key()));
        if (area >= kAreaCount || !it.value().is_array()) {
            continue;
        }

        auto& out = gAssetGroupCache.areaTables[area];
        for (const auto& jsonRoom : it.value()) {
            AreaPropertyEntryData roomEntry;
            const nlohmann::json* filesJson = nullptr;

            if (jsonRoom.is_array()) {
                filesJson = &jsonRoom;
            } else if (jsonRoom.is_object() && jsonRoom.contains("files") && jsonRoom["files"].is_array()) {
                filesJson = &jsonRoom["files"];
            }

            if (filesJson != nullptr) {
                for (const auto& jsonFile : *filesJson) {
                    if (jsonFile.is_string()) {
                        roomEntry.files.push_back(jsonFile.get<std::string>());
                    } else {
                        roomEntry.files.emplace_back();
                    }
                }
            }

            out.push_back(std::move(roomEntry));
        }
    }
}

void ParseSpritePtrs(const nlohmann::json& root) {
    gAssetGroupCache.spritePtrs.clear();

    if (root.is_array()) {
        gAssetGroupCache.spritePtrs.resize(root.size());
        for (size_t i = 0; i < root.size(); ++i) {
            const auto& jsonEntry = root[i];
            if (!jsonEntry.is_object()) {
                continue;
            }

            SpritePtrEntryData entry = {};
            if (jsonEntry.contains("animations") && jsonEntry["animations"].is_array()) {
                for (const auto& jsonAnim : jsonEntry["animations"]) {
                    if (jsonAnim.is_string()) {
                        entry.animations.push_back(jsonAnim.get<std::string>());
                    } else {
                        entry.animations.emplace_back();
                    }
                }
            }
            entry.framesFile = JsonStringOrEmpty(jsonEntry, "frames_file");
            entry.ptrFile = JsonStringOrEmpty(jsonEntry, "ptr_file");
            entry.pad = jsonEntry.value("pad", 0u);
            gAssetGroupCache.spritePtrs[i] = std::move(entry);
        }
        return;
    }

    if (!root.is_object()) {
        return;
    }

    size_t maxIndex = 0;
    for (auto it = root.begin(); it != root.end(); ++it) {
        maxIndex = std::max(maxIndex, static_cast<size_t>(std::stoul(it.key())));
    }

    gAssetGroupCache.spritePtrs.resize(maxIndex + 1);
    for (auto it = root.begin(); it != root.end(); ++it) {
        const size_t index = static_cast<size_t>(std::stoul(it.key()));
        if (!it.value().is_object()) {
            continue;
        }

        SpritePtrEntryData entry = {};
        if (it.value().contains("animations") && it.value()["animations"].is_array()) {
            for (const auto& jsonAnim : it.value()["animations"]) {
                if (jsonAnim.is_string()) {
                    entry.animations.push_back(jsonAnim.get<std::string>());
                } else {
                    entry.animations.emplace_back();
                }
            }
        }
        entry.framesFile = JsonStringOrEmpty(it.value(), "frames_file");
        entry.ptrFile = JsonStringOrEmpty(it.value(), "ptr_file");
        entry.pad = it.value().value("pad", 0u);
        gAssetGroupCache.spritePtrs[index] = std::move(entry);
    }
}

void WriteLe32(std::vector<u8>& buffer, size_t offset, u32 value) {
    if (offset + 4 > buffer.size()) {
        return;
    }
    buffer[offset + 0] = static_cast<u8>(value & 0xFF);
    buffer[offset + 1] = static_cast<u8>((value >> 8) & 0xFF);
    buffer[offset + 2] = static_cast<u8>((value >> 16) & 0xFF);
    buffer[offset + 3] = static_cast<u8>((value >> 24) & 0xFF);
}

bool BuildTranslationBufferFromJson(const nlohmann::json& languageJson, std::vector<u8>& outBuffer) {
    outBuffer.clear();

    const u32 categoryCount = languageJson.value("category_count", 0u);
    if (categoryCount == 0 || !languageJson.contains("categories") || !languageJson["categories"].is_object()) {
        return false;
    }

    outBuffer.resize(static_cast<size_t>(categoryCount) * 4, 0);

    for (auto catIt = languageJson["categories"].begin(); catIt != languageJson["categories"].end(); ++catIt) {
        const u32 categoryIndex = static_cast<u32>(std::stoul(catIt.key()));
        if (categoryIndex >= categoryCount) {
            continue;
        }

        const nlohmann::json& categoryJson = catIt.value();
        const u32 messageCount = categoryJson.value("message_count", 0u);
        std::vector<u8> categoryBuffer(static_cast<size_t>(messageCount) * 4, 0);
        size_t categoryWritePos = categoryBuffer.size();

        if (categoryJson.contains("messages") && categoryJson["messages"].is_array()) {
            for (const auto& messageJson : categoryJson["messages"]) {
                const u32 messageIndex = messageJson.value("index", 0u);
                if (messageIndex >= messageCount) {
                    continue;
                }

                const std::string file = JsonStringOrEmpty(messageJson, "file");
                if (file.empty()) {
                    continue;
                }

                const std::vector<u8>* fileData = LoadBinaryFileCached(file);
                if (fileData == nullptr) {
                    return false;
                }

                WriteLe32(categoryBuffer, static_cast<size_t>(messageIndex) * 4, static_cast<u32>(categoryWritePos));
                categoryBuffer.insert(categoryBuffer.end(), fileData->begin(), fileData->end());
                categoryWritePos += fileData->size();
            }
        }

        while ((categoryBuffer.size() & 0xF) != 0) {
            categoryBuffer.push_back(0xFF);
        }

        const u32 categoryOffset = static_cast<u32>(outBuffer.size());
        WriteLe32(outBuffer, static_cast<size_t>(categoryIndex) * 4, categoryOffset);
        outBuffer.insert(outBuffer.end(), categoryBuffer.begin(), categoryBuffer.end());
    }

    while ((outBuffer.size() & 0xF) != 0) {
        outBuffer.push_back(0xFF);
    }

    return true;
}

void ParseTexts(const nlohmann::json& root) {
    for (std::vector<u8>& buffer : gAssetGroupCache.translationBuffers) {
        buffer.clear();
    }
    for (auto& files : gAssetGroupCache.textFilesById) {
        files.clear();
    }

    if (!root.is_object()) {
        return;
    }

    for (auto langIt = root.begin(); langIt != root.end(); ++langIt) {
        const u32 languageIndex = static_cast<u32>(std::stoul(langIt.key()));
        if (languageIndex >= gAssetGroupCache.translationBuffers.size()) {
            continue;
        }

        const nlohmann::json& languageJson = langIt.value();
        if (!languageJson.value("valid", false)) {
            continue;
        }

        const std::string tableFile = JsonStringOrEmpty(languageJson, "table_file");
        if (!tableFile.empty()) {
            const std::vector<u8>* tableData = LoadBinaryFileCached(tableFile);
            if (tableData != nullptr) {
                gAssetGroupCache.translationBuffers[languageIndex] = *tableData;
            }
        }
        if (gAssetGroupCache.translationBuffers[languageIndex].empty()) {
            BuildTranslationBufferFromJson(languageJson, gAssetGroupCache.translationBuffers[languageIndex]);
        }

        if (languageJson.contains("categories") && languageJson["categories"].is_object()) {
            for (auto catIt = languageJson["categories"].begin(); catIt != languageJson["categories"].end(); ++catIt) {
                const u32 categoryIndex = static_cast<u32>(std::stoul(catIt.key()));
                const nlohmann::json& categoryJson = catIt.value();
                if (!categoryJson.contains("messages") || !categoryJson["messages"].is_array()) {
                    continue;
                }

                for (const auto& messageJson : categoryJson["messages"]) {
                    const u32 messageIndex = messageJson.value("index", 0u);
                    const u32 textId = messageJson.value("text_id", (categoryIndex << 8) | messageIndex);
                    const std::string file = JsonStringOrEmpty(messageJson, "file");
                    if (!file.empty()) {
                        gAssetGroupCache.textFilesById[languageIndex][textId] = file;
                    }
                }
            }
        }
    }
}

const std::vector<u8>* LoadBinaryFileCached(const std::string& relativePath) {
    auto it = gAssetGroupCache.binaryFiles.find(relativePath);
    if (it != gAssetGroupCache.binaryFiles.end()) {
        return it->second.get();
    }

    const std::filesystem::path fullPath = gAssetGroupCache.assetsRoot / std::filesystem::path(relativePath);
    std::ifstream input(fullPath, std::ios::binary);
    if (!input.good()) {
        return nullptr;
    }

    auto data = std::make_unique<std::vector<u8>>(std::istreambuf_iterator<char>(input),
                                                  std::istreambuf_iterator<char>());
    const std::vector<u8>* result = data.get();
    gAssetGroupCache.binaryFiles.emplace(relativePath, std::move(data));
    return result;
}

/* Look up a property's ROM offset by file path so we can serve it directly
 * from gRomData. Some additional_X room properties (HouseDoor data tables,
 * for example) span multiple consecutive `_N.bin` chunks with inline 4-byte
 * GBA script pointers between them. The asset extractor splits those chunks
 * into separate files and area_tables.json references only the first chunk,
 * so iteration past the first file's bytes hits past-end memory and reads
 * a garbage 4th door. Pointing at gRomData (already populated by both the
 * baserom.gba load and the .bin overwrites) gives the contiguous, correctly-
 * terminated layout instead. (#28 root cause.) */
extern "C" const EmbeddedAssetEntry* EmbeddedAssetIndex_Get(void);
extern "C" u32 EmbeddedAssetIndex_Count(void);
static u8* RomBackedPointerForAssetFile(const std::string& relativePath) {
    if (gRomData == nullptr) {
        return nullptr;
    }
    static std::unordered_map<std::string, u32> sFileToRomOffset = []() {
        std::unordered_map<std::string, u32> map;
        const EmbeddedAssetEntry* entries = EmbeddedAssetIndex_Get();
        const u32 count = EmbeddedAssetIndex_Count();
        map.reserve(count);
        for (u32 i = 0; i < count; ++i) {
            map.emplace(entries[i].path, entries[i].offset);
        }
        return map;
    }();
    auto it = sFileToRomOffset.find(relativePath);
    if (it == sFileToRomOffset.end()) {
        return nullptr;
    }
    if (it->second >= gRomSize) {
        return nullptr;
    }
    return gRomData + it->second;
}

u32 RegisterMapAssetFile(const std::string& relativePath) {
    auto found = gAssetGroupCache.mapAssetFileToIndex.find(relativePath);
    if (found != gAssetGroupCache.mapAssetFileToIndex.end()) {
        return found->second;
    }

    const u32 index = static_cast<u32>(gAssetGroupCache.mapAssetFiles.size());
    gAssetGroupCache.mapAssetFiles.push_back(relativePath);
    gAssetGroupCache.mapAssetFileToIndex.emplace(relativePath, index);
    return index;
}

MapDataDefinition* BuildMapDefinitionSequence(const std::vector<MapDefinitionRefData>& refs, u32 area) {
    if (refs.empty()) {
        return nullptr;
    }

    auto defs = std::make_unique<MapDataDefinition[]>(refs.size());
    for (size_t i = 0; i < refs.size(); ++i) {
        const MapDefinitionRefData& ref = refs[i];
        defs[i].dest = reinterpret_cast<void*>(static_cast<uintptr_t>(ref.dest));

        if (ref.isPaletteGroup) {
            defs[i].src = (ref.multiple ? MAP_MULTIPLE : 0u) | ref.paletteGroup;
            defs[i].dest = nullptr;
            defs[i].size = 0;
            continue;
        }

        const u32 assetIndex = RegisterMapAssetFile(ref.file);
        defs[i].src = (ref.multiple ? MAP_MULTIPLE : 0u) | MAP_SRC_FILE | assetIndex;
        defs[i].size = ref.size | (ref.compressed ? MAP_COMPRESSED : 0u);
    }

    MapDataDefinition* result = defs.get();
    gAssetGroupCache.mapDefStorage[area].push_back(std::move(defs));
    return result;
}

bool BuildAreaFromAssets(u32 area) {
    if (area >= kAreaCount || !gAssetGroupCache.hasAreaData) {
        return false;
    }

    gAssetGroupCache.areaTileSetPtrs[area].clear();
    gAssetGroupCache.areaRoomMapPtrs[area].clear();
    gAssetGroupCache.areaTablePtrs[area].clear();
    gAssetGroupCache.areaPropertyStorage[area].clear();
    gAssetGroupCache.mapDefStorage[area].clear();
    gAssetGroupCache.areaTilesPtrs[area] = nullptr;

    if (!gAssetGroupCache.areaRoomHeaders[area].empty()) {
        gAreaRoomHeaders[area] = gAssetGroupCache.areaRoomHeaders[area].data();
    } else {
        gAreaRoomHeaders[area] = nullptr;
    }

    const size_t tileSetSlots = std::max<size_t>(gAssetGroupCache.areaTileSets[area].size(), 64);
    gAssetGroupCache.areaTileSetPtrs[area].assign(tileSetSlots, nullptr);
    for (size_t i = 0; i < gAssetGroupCache.areaTileSets[area].size(); ++i) {
        gAssetGroupCache.areaTileSetPtrs[area][i] = BuildMapDefinitionSequence(gAssetGroupCache.areaTileSets[area][i], area);
    }
    gAreaTileSets[area] =
        gAssetGroupCache.areaTileSetPtrs[area].empty() ? nullptr : gAssetGroupCache.areaTileSetPtrs[area].data();

    const size_t roomMapSlots = std::max<size_t>(gAssetGroupCache.areaRoomMaps[area].size(), 64);
    gAssetGroupCache.areaRoomMapPtrs[area].assign(roomMapSlots, nullptr);
    for (size_t i = 0; i < gAssetGroupCache.areaRoomMaps[area].size(); ++i) {
        gAssetGroupCache.areaRoomMapPtrs[area][i] = BuildMapDefinitionSequence(gAssetGroupCache.areaRoomMaps[area][i], area);
    }
    gAreaRoomMaps[area] =
        gAssetGroupCache.areaRoomMapPtrs[area].empty() ? nullptr : gAssetGroupCache.areaRoomMapPtrs[area].data();

    gAssetGroupCache.areaTilesPtrs[area] = BuildMapDefinitionSequence(gAssetGroupCache.areaTiles[area], area);
    gAreaTiles[area] = gAssetGroupCache.areaTilesPtrs[area];

    const auto& jsonTables = gAssetGroupCache.areaTables[area];
    const size_t areaTableSlots = std::max<size_t>(jsonTables.size(), 64);
    gAssetGroupCache.areaPropertyStorage[area].clear();
    gAssetGroupCache.areaPropertyStorage[area].resize(areaTableSlots);
    gAssetGroupCache.areaTablePtrs[area].assign(areaTableSlots, nullptr);
    for (size_t room = 0; room < jsonTables.size(); ++room) {
        const AreaPropertyEntryData& roomEntry = jsonTables[room];
        const size_t propertySlots = std::max<size_t>(roomEntry.files.size(), 64);
        auto props = std::make_unique<void*[]>(propertySlots);
        for (size_t i = 0; i < propertySlots; ++i) {
            props[i] = nullptr;
            if (i >= roomEntry.files.size()) {
                continue;
            }
            if (roomEntry.files[i].empty()) {
                continue;
            }

            /* Prefer pointing into gRomData when the file is a ROM-backed
             * asset — fixes split additional_* property tables (#28). */
            if (u8* romPtr = RomBackedPointerForAssetFile(roomEntry.files[i])) {
                props[i] = romPtr;
            } else {
                const std::vector<u8>* fileData = LoadBinaryFileCached(roomEntry.files[i]);
                if (fileData != nullptr && !fileData->empty()) {
                    props[i] = const_cast<u8*>(fileData->data());
                }
            }
        }

        gAssetGroupCache.areaTablePtrs[area][room] = props.get();
        gAssetGroupCache.areaPropertyStorage[area][room] = std::move(props);
    }
    gAreaTable[area] =
        gAssetGroupCache.areaTablePtrs[area].empty() ? nullptr : gAssetGroupCache.areaTablePtrs[area].data();

    return true;
}

void RefreshSprite322DerivedTables() {
    memset(gMoreSpritePtrs, 0, sizeof(u16*) * 16);
    memset(gSpriteAnimations_322, 0, sizeof(Frame*) * kSpriteAnim322Count);

    if (gAssetGroupCache.spriteAnimationPtrs.size() <= 322) {
        return;
    }

    const SpritePtr& sp322 = gSpritePtrs[322];
    gMoreSpritePtrs[0] = reinterpret_cast<u16*>(sp322.animations);
    gMoreSpritePtrs[1] = reinterpret_cast<u16*>(sp322.frames);
    gMoreSpritePtrs[2] = reinterpret_cast<u16*>(sp322.ptr);

    const auto& anims = gAssetGroupCache.spriteAnimationPtrs[322];
    const size_t count = std::min(anims.size(), static_cast<size_t>(kSpriteAnim322Count));
    for (size_t i = 0; i < count; ++i) {
        gSpriteAnimations_322[i] = reinterpret_cast<Frame*>(const_cast<u8*>(anims[i]));
    }
}

bool EnsureAssetGroupCache() {
    if (gAssetGroupCache.initAttempted) {
        return gAssetGroupCache.ready;
    }

    gAssetGroupCache.initAttempted = true;

    const std::optional<std::filesystem::path> editableRoot = FindEditableAssetsRoot();
    std::optional<std::filesystem::path> assetsRoot;

    if (editableRoot.has_value()) {
        const std::filesystem::path runtimeRoot = RuntimeRootForEditableRoot(*editableRoot);
        std::string buildInfo;
        if (!PortAssetPipeline::EnsureRuntimeAssetsBuilt(*editableRoot, runtimeRoot, &buildInfo)) {
            std::fprintf(stderr, "[ASSET] Failed to build runtime assets from %s: %s\n",
                         editableRoot->string().c_str(), buildInfo.c_str());
            return false;
        }

        if (!buildInfo.empty()) {
            std::fprintf(stderr, "[ASSET] Rebuilt runtime assets from %s (%s)\n", editableRoot->string().c_str(),
                         buildInfo.c_str());
        }

        assetsRoot = runtimeRoot;
    } else {
        assetsRoot = FindRuntimeAssetsRoot();
    }

    if (!assetsRoot.has_value()) {
        return false;
    }

    nlohmann::json gfxGroupsJson;
    nlohmann::json paletteGroupsJson;
    nlohmann::json areaRoomHeadersJson;
    nlohmann::json areaTileSetsJson;
    nlohmann::json areaRoomMapsJson;
    nlohmann::json areaTablesJson;
    nlohmann::json areaTilesJson;
    nlohmann::json spritePtrsJson;
    nlohmann::json textsJson;

    if (!LoadJsonFile(*assetsRoot / "gfx_groups.json", gfxGroupsJson) ||
        !LoadJsonFile(*assetsRoot / "palette_groups.json", paletteGroupsJson) ||
        !LoadOptionalJson(*assetsRoot / "area_room_headers.json", areaRoomHeadersJson) ||
        !LoadOptionalJson(*assetsRoot / "area_tile_sets.json", areaTileSetsJson) ||
        !LoadOptionalJson(*assetsRoot / "area_room_maps.json", areaRoomMapsJson) ||
        !LoadOptionalJson(*assetsRoot / "area_tables.json", areaTablesJson) ||
        !LoadOptionalJson(*assetsRoot / "area_tiles.json", areaTilesJson) ||
        !LoadOptionalJson(*assetsRoot / "sprite_ptrs.json", spritePtrsJson) ||
        !LoadOptionalJson(*assetsRoot / "texts.json", textsJson)) {
        return false;
    }

    gAssetGroupCache.assetsRoot = *assetsRoot;
    gAssetGroupCache.hasAreaData = !areaRoomHeadersJson.is_null() && !areaTileSetsJson.is_null() &&
                                   !areaRoomMapsJson.is_null() && !areaTablesJson.is_null() && !areaTilesJson.is_null();
    gAssetGroupCache.hasSpritePtrData = !spritePtrsJson.is_null();
    gAssetGroupCache.hasTextData = !textsJson.is_null();

    try {
        ParseGfxGroups(gfxGroupsJson);
        ParsePaletteGroups(paletteGroupsJson);
        if (gAssetGroupCache.hasAreaData) {
            ParseAreaRoomHeaders(areaRoomHeadersJson);
            ParseAreaMapTable(areaTileSetsJson, gAssetGroupCache.areaTileSets);
            ParseAreaMapTable(areaRoomMapsJson, gAssetGroupCache.areaRoomMaps);
            ParseAreaTiles(areaTilesJson);
            ParseAreaTables(areaTablesJson);
        }
        if (gAssetGroupCache.hasSpritePtrData) {
            ParseSpritePtrs(spritePtrsJson);
        } else {
            gAssetGroupCache.spritePtrs.clear();
        }
        if (gAssetGroupCache.hasTextData) {
            ParseTexts(textsJson);
        } else {
            for (std::vector<u8>& buffer : gAssetGroupCache.translationBuffers) {
                buffer.clear();
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[ASSET] JSON parse failed in %s: %s\n", gAssetGroupCache.assetsRoot.string().c_str(),
                     e.what());
        return false;
    }

    gAssetGroupCache.ready = true;
    return true;
}

extern "C" void Port_LogAssetLoaderStatus(void) {
    const std::optional<std::filesystem::path> editableRoot = FindEditableAssetsRoot();
    const std::optional<std::filesystem::path> runtimeRoot = FindRuntimeAssetsRoot();

    AssetLogOnce("startup-banner", "startup asset scan:");
    AssetLogOnce("startup-editable",
                 "editable root: %s",
                 editableRoot.has_value() ? PathForLog(*editableRoot).c_str() : "<none>");
    AssetLogOnce("startup-runtime",
                 "runtime root candidate: %s",
                 runtimeRoot.has_value() ? PathForLog(*runtimeRoot).c_str() : "<none>");

    if (!EnsureAssetGroupCache()) {
        AssetLogOnce("startup-disabled", "asset loader inactive; ROM will be used for tables and animations.");
        return;
    }

    AssetLogOnce("startup-root", "selected asset root: %s", PathForLog(gAssetGroupCache.assetsRoot).c_str());
    AssetLogOnce("startup-gfx", "gfx groups: enabled (%zu groups)", gAssetGroupCache.gfxGroups.size());
    AssetLogOnce("startup-pal", "palette groups: enabled (%zu groups)", gAssetGroupCache.paletteGroups.size());
    AssetLogOnce("startup-sprite",
                 "sprite_ptrs: %s",
                 gAssetGroupCache.hasSpritePtrData ? "enabled via sprite_ptrs.json" : "disabled, ROM fallback");
    AssetLogOnce("startup-text",
                 "texts: %s",
                 gAssetGroupCache.hasTextData ? "enabled via texts.json" : "disabled, ROM fallback");
    AssetLogOnce("startup-area",
                 "area tables: %s",
                 gAssetGroupCache.hasAreaData ? "enabled via area_*.json" : "disabled, ROM fallback");
    AssetLogOnce("startup-map-assets", "registered map asset files: %zu", gAssetGroupCache.mapAssetFiles.size());
}

enum GfxLoadDecision {
    GFX_SKIP = 0,
    GFX_LOAD = 1,
    GFX_STOP = 2,
};

GfxLoadDecision EvaluateGfxControl(u8 unknown) {
    const SaveHeaderLite* saveHeader = static_cast<const SaveHeaderLite*>(gba_MemPtr(0x02000000u));
    const u8 language = (saveHeader != nullptr) ? saveHeader->language : 0;
    const u32 ctrl = unknown & 0xF;

    switch (ctrl) {
        case 0x7:
            return GFX_LOAD;
        case 0xD:
            return GFX_STOP;
        case 0xE:
            return (language != 0 && language != 1) ? GFX_LOAD : GFX_SKIP;
        case 0xF:
            return (language != 0) ? GFX_LOAD : GFX_SKIP;
        default:
            return (ctrl == language) ? GFX_LOAD : GFX_SKIP;
    }
}

} // namespace

extern "C" bool32 Port_LoadPaletteGroupFromAssets(u32 group) {
    if (!EnsureAssetGroupCache()) {
        return FALSE;
    }

    const auto it = gAssetGroupCache.paletteGroups.find(group);
    if (it == gAssetGroupCache.paletteGroups.end()) {
        return FALSE;
    }

    AssetLogOnce("palette-group-json:" + std::to_string(group), "palette group %u described by %s/palette_groups.json",
                 group, PathForLog(gAssetGroupCache.assetsRoot).c_str());

    for (const PaletteGroupEntryData& entry : it->second) {
        u32 copiedPalettes = 0;

        for (const PaletteFileRefData& ref : entry.paletteFiles) {
            const std::vector<u8>* fileData = LoadBinaryFileCached(ref.file);
            if (fileData == nullptr || ref.byteOffset + ref.size > fileData->size()) {
                return FALSE;
            }

            AssetLogOnce("palette-file:" + std::to_string(group) + ":" + std::to_string(entry.destPaletteNum + copiedPalettes) +
                             ":" + ref.file,
                         "palette group %u slot %u <- %s", group, entry.destPaletteNum + copiedPalettes, ref.file.c_str());
            LoadPalettes(fileData->data() + ref.byteOffset, entry.destPaletteNum + copiedPalettes, ref.numPalettes);
            copiedPalettes += ref.numPalettes;
        }

        if (copiedPalettes != entry.numPalettes) {
            return FALSE;
        }

        if (entry.terminator) {
            break;
        }
    }

    return TRUE;
}

extern "C" bool32 Port_LoadGfxGroupFromAssets(u32 group) {
    if (!EnsureAssetGroupCache()) {
        return FALSE;
    }

    const auto it = gAssetGroupCache.gfxGroups.find(group);
    if (it == gAssetGroupCache.gfxGroups.end()) {
        return FALSE;
    }

    AssetLogOnce("gfx-group-json:" + std::to_string(group), "gfx group %u described by %s/gfx_groups.json", group,
                 PathForLog(gAssetGroupCache.assetsRoot).c_str());

    for (const GfxGroupEntryData& entry : it->second) {
        const GfxLoadDecision decision = EvaluateGfxControl(entry.unknown);

        if (decision == GFX_STOP) {
            return TRUE;
        }

        if (decision == GFX_LOAD && !entry.file.empty()) {
            const std::vector<u8>* fileData = LoadBinaryFileCached(entry.file);
            if (fileData == nullptr) {
                return FALSE;
            }

            AssetLogOnce("gfx-file:" + std::to_string(group) + ":" + entry.file + ":" + std::to_string(entry.dest),
                         "gfx group %u -> %s (dest=0x%08X, %u bytes)", group, entry.file.c_str(), entry.dest,
                         static_cast<u32>(fileData->size()));
            /* MemCopy resolves dest via port_resolve_addr (raw gEwram[N] for
             * EWRAM addresses), but the port has heap-allocated stand-in
             * arrays for gMapDataBottomSpecial / gMapDataTopSpecial / gMapTop
             * etc. that live OUTSIDE gEwram. Use Port_ResolveEwramPtr for
             * EWRAM destinations so writes hit the actual game variables;
             * fall back to MemCopy's generic path for VRAM/IWRAM. */
            void* resolvedDest = nullptr;
            if (entry.dest >= 0x02000000u && entry.dest < 0x02040000u) {
                resolvedDest = Port_ResolveEwramPtr(entry.dest);
            }
            if (resolvedDest != nullptr) {
                std::memcpy(resolvedDest, fileData->data(), fileData->size());
            } else {
                MemCopy(fileData->data(), reinterpret_cast<void*>(static_cast<uintptr_t>(entry.dest)),
                        static_cast<u32>(fileData->size()));
            }
        }

        if (entry.terminator) {
            break;
        }
    }

    return TRUE;
}

extern "C" bool32 Port_LoadAreaTablesFromAssets(void) {
    if (!EnsureAssetGroupCache() || !gAssetGroupCache.hasAreaData) {
        return FALSE;
    }

    AssetLogOnce("area-data-json", "area tables enabled from %s/{area_room_headers.json,area_tile_sets.json,area_room_maps.json,area_tables.json,area_tiles.json}",
                 PathForLog(gAssetGroupCache.assetsRoot).c_str());

    for (u32 area = 0; area < kAreaCount; ++area) {
        BuildAreaFromAssets(area);
    }

    gAssetGroupCache.areaTablesLoaded = true;
    return TRUE;
}

extern "C" bool32 Port_LoadSpritePtrsFromAssets(void) {
    if (!EnsureAssetGroupCache() || !gAssetGroupCache.hasSpritePtrData || gAssetGroupCache.spritePtrs.empty()) {
        return FALSE;
    }

    AssetLogOnce("sprite-ptrs-json", "sprite pointer table enabled from %s/sprite_ptrs.json",
                 PathForLog(gAssetGroupCache.assetsRoot).c_str());

    std::vector<std::vector<const u8*>> newAnimationPtrs;
    newAnimationPtrs.resize(std::max(kSpritePtrMax, gAssetGroupCache.spritePtrs.size()));
    std::vector<SpritePtr> newSpritePtrs(kSpritePtrMax);

    for (size_t i = 0; i < gAssetGroupCache.spritePtrs.size() && i < kSpritePtrMax; ++i) {
        const SpritePtrEntryData& entry = gAssetGroupCache.spritePtrs[i];
        SpritePtr sprite = {};

        if (!entry.framesFile.empty()) {
            const std::vector<u8>* framesData = LoadBinaryFileCached(entry.framesFile);
            if (framesData == nullptr) {
                return FALSE;
            }
            sprite.frames = reinterpret_cast<SpriteFrame*>(const_cast<u8*>(framesData->data()));
        }

        if (!entry.ptrFile.empty()) {
            const std::vector<u8>* ptrData = LoadBinaryFileCached(entry.ptrFile);
            if (ptrData == nullptr) {
                return FALSE;
            }
            sprite.ptr = const_cast<u8*>(ptrData->data());
        }

        auto& animPtrs = newAnimationPtrs[i];
        animPtrs.clear();
        animPtrs.reserve(entry.animations.size());
        std::vector<const std::vector<u8>*> rawAnims;
        rawAnims.reserve(entry.animations.size());
        for (const std::string& animFile : entry.animations) {
            if (animFile.empty()) {
                rawAnims.push_back(nullptr);
                continue;
            }
            const std::vector<u8>* animData = LoadBinaryFileCached(animFile);
            if (animData == nullptr) {
                return FALSE;
            }
            rawAnims.push_back(animData);
        }
        for (size_t a = 0; a < rawAnims.size(); ++a) {
            const std::vector<u8>* animData = rawAnims[a];
            if (animData == nullptr) {
                animPtrs.push_back(nullptr);
                continue;
            }
            const u8* dataPtr = animData->data();
            const size_t dataSize = animData->size();
            /* If the last 4-byte frame's bit-7 is set, the runtime expects a
             * trailing loop_back byte. On GBA that byte is the first byte
             * of the next animation in ROM (which the assets extractor drops
             * because it sizes each anim by ROM offset diff). Reconstruct
             * by copying the .bin and appending the next animation's first
             * byte (or 0 if there isn't one). See port_animation.c FrameZero. */
            if (dataSize >= 4 && (dataSize % 4u) == 0u && (dataPtr[dataSize - 1] & 0x80u)) {
                /* Default: loop to the start of this animation (= numFrames).
                 * If the next animation's first byte falls within this anim's
                 * frame count, prefer that — it matches the byte the GBA
                 * reads from the adjacent ROM region. Out-of-range values
                 * (which happen because not every anim was meant to loop
                 * naturally — actions transition out before the loop fires)
                 * fall back to the safe "loop to start". */
                const size_t numFrames = dataSize / 4u;
                u8 loopBack = static_cast<u8>(std::min<size_t>(numFrames, 0xFFu));
                if (a + 1 < rawAnims.size() && rawAnims[a + 1] != nullptr && !rawAnims[a + 1]->empty()) {
                    u8 nextByte = (*rawAnims[a + 1])[0];
                    if (nextByte > 0 && nextByte <= numFrames) {
                        loopBack = nextByte;
                    }
                }
                auto buf = std::make_unique<std::vector<u8>>();
                buf->reserve(dataSize + 1);
                buf->assign(dataPtr, dataPtr + dataSize);
                buf->push_back(loopBack);
                const u8* paddedPtr = buf->data();
                gAssetGroupCache.animPaddedBuffers.push_back(std::move(buf));
                animPtrs.push_back(paddedPtr);
            } else {
                animPtrs.push_back(dataPtr);
            }
        }

        sprite.animations = animPtrs.empty() ? nullptr : (void*)animPtrs.data();
        sprite.pad = entry.pad;
        newSpritePtrs[i] = sprite;
    }

    gAssetGroupCache.spriteAnimationPtrs = std::move(newAnimationPtrs);
    memset(gSpritePtrs, 0, sizeof(SpritePtr) * kSpritePtrMax);
    for (size_t i = 0; i < newSpritePtrs.size(); ++i) {
        gSpritePtrs[i] = newSpritePtrs[i];
    }

    RefreshSprite322DerivedTables();
    gAssetGroupCache.spritePtrsLoaded = true;
    return TRUE;
}

extern "C" bool32 Port_LoadTextsFromAssets(void) {
    if (!EnsureAssetGroupCache() || !gAssetGroupCache.hasTextData) {
        return FALSE;
    }

    bool anyLoaded = false;
    for (size_t i = 0; i < gAssetGroupCache.translationBuffers.size(); ++i) {
        std::vector<u8>& buffer = gAssetGroupCache.translationBuffers[i];
        if (buffer.empty()) {
            gTranslations[i] = nullptr;
            continue;
        }

        gTranslations[i] = reinterpret_cast<u32*>(buffer.data());
        anyLoaded = true;
    }

    if (anyLoaded) {
        gAssetGroupCache.textsLoaded = true;
        AssetLogOnce("texts-root", "translations loaded from %s", PathForLog(gAssetGroupCache.assetsRoot / "texts.json").c_str());
    }

    return anyLoaded ? TRUE : FALSE;
}

extern "C" void Port_LogTextLookup(u32 langIndex, u32 textIndex) {
    const std::string key = "text-lookup:" + std::to_string(langIndex) + ":" + std::to_string(textIndex);

    if (!EnsureAssetGroupCache() || !gAssetGroupCache.hasTextData || langIndex >= gAssetGroupCache.textFilesById.size()) {
        AssetLogOnce(key, "text 0x%04X lang %u <- ROM", textIndex & 0xFFFF, langIndex);
        return;
    }

    const auto it = gAssetGroupCache.textFilesById[langIndex].find(textIndex & 0xFFFF);
    if (it != gAssetGroupCache.textFilesById[langIndex].end()) {
        AssetLogOnce(key, "text 0x%04X lang %u <- %s", textIndex & 0xFFFF, langIndex, it->second.c_str());
    } else {
        AssetLogOnce(key, "text 0x%04X lang %u <- extracted table (file unknown)", textIndex & 0xFFFF, langIndex);
    }
}

extern "C" bool32 Port_AreSpritePtrsLoadedFromAssets(void) {
    return gAssetGroupCache.spritePtrsLoaded ? TRUE : FALSE;
}

extern "C" bool32 Port_RefreshAreaDataFromAssets(u32 area) {
    if (!EnsureAssetGroupCache() || !gAssetGroupCache.hasAreaData || area >= kAreaCount) {
        return FALSE;
    }

    AssetLogOnce("area-refresh:" + std::to_string(area), "area %u refreshed from extracted area tables", area);
    return BuildAreaFromAssets(area) ? TRUE : FALSE;
}

extern "C" bool32 Port_IsRoomHeaderPtrReadable(const void* ptr) {
    if (ptr == nullptr) {
        return FALSE;
    }

    if (IsRomPointer(ptr, sizeof(RoomHeader))) {
        return TRUE;
    }

    const RoomHeader* roomPtr = static_cast<const RoomHeader*>(ptr);

    for (const auto& roomHeaders : gAssetGroupCache.areaRoomHeaders) {
        if (roomHeaders.empty()) {
            continue;
        }

        const RoomHeader* begin = roomHeaders.data();
        const RoomHeader* end = begin + roomHeaders.size();
        if (roomPtr >= begin && roomPtr < end) {
            return TRUE;
        }
    }

    return FALSE;
}

extern "C" bool32 Port_IsLoadedAssetBytes(const void* ptr, u32 size) {
    if (ptr == nullptr) {
        return FALSE;
    }

    for (const auto& [_, dataPtr] : gAssetGroupCache.binaryFiles) {
        if (dataPtr == nullptr || dataPtr->empty()) {
            continue;
        }

        const u8* begin = dataPtr->data();
        const u8* end = begin + dataPtr->size();
        const u8* at = static_cast<const u8*>(ptr);
        if (at >= begin && at <= end && size <= static_cast<u32>(end - at)) {
            return TRUE;
        }
    }

    return FALSE;
}

extern "C" const u8* Port_GetMapAssetDataByIndex(u32 assetIndex, u32* size) {
    if (!EnsureAssetGroupCache() || assetIndex >= gAssetGroupCache.mapAssetFiles.size()) {
        return nullptr;
    }

    const std::vector<u8>* fileData = LoadBinaryFileCached(gAssetGroupCache.mapAssetFiles[assetIndex]);
    if (fileData == nullptr) {
        return nullptr;
    }

    if (size != nullptr) {
        *size = static_cast<u32>(fileData->size());
    }
    AssetLogOnce("map-asset:" + std::to_string(assetIndex), "map asset %u <- %s", assetIndex,
                 gAssetGroupCache.mapAssetFiles[assetIndex].c_str());
    return fileData->data();
}

extern "C" const u8* Port_GetSpriteAnimationData(u16 spriteIndex, u32 animIndex) {
    if (EnsureAssetGroupCache()) {
        if (!gAssetGroupCache.spritePtrsLoaded) {
            Port_LoadSpritePtrsFromAssets();
        }

        if (gAssetGroupCache.spritePtrsLoaded && spriteIndex < gAssetGroupCache.spriteAnimationPtrs.size()) {
            const auto& anims = gAssetGroupCache.spriteAnimationPtrs[spriteIndex];
            if (animIndex < anims.size()) {
                if (spriteIndex < gAssetGroupCache.spritePtrs.size() &&
                    animIndex < gAssetGroupCache.spritePtrs[spriteIndex].animations.size()) {
                    AssetLogOnce("sprite-anim:" + std::to_string(spriteIndex) + ":" + std::to_string(animIndex),
                                 "sprite %u anim %u <- %s", spriteIndex, animIndex,
                                 gAssetGroupCache.spritePtrs[spriteIndex].animations[animIndex].c_str());
                }
                return anims[animIndex];
            }
        }
    }

    const SpritePtr* spr = Port_GetSpritePtr(spriteIndex);
    if (spr == nullptr || spr->animations == nullptr) {
        return nullptr;
    }

    const u8* animTable = static_cast<const u8*>(spr->animations);
    const size_t tableBytes = (static_cast<size_t>(animIndex) + 1u) * sizeof(u32);
    if (!IsRomPointer(animTable, tableBytes)) {
        return nullptr;
    }

    const u32 animGbaAddr = Port_ReadU32(animTable + static_cast<size_t>(animIndex) * sizeof(u32));
    if (animGbaAddr == 0) {
        return nullptr;
    }

    return static_cast<const u8*>(Port_ResolveRomData(animGbaAddr));
}
