#pragma once

#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <optional>
#include <functional>
#include <unordered_map>
#include <set>
#include <nlohmann/json.hpp>
#include "port_asset_pipeline.hpp"

extern "C" {
#define this this_
#include "port_asset_index.h"
#undef this
}

struct Config
{
    uint32_t gfxGroupsTableOffset;
    uint32_t gfxGroupsTableLength;
    uint32_t paletteGroupsTableOffset;
    uint32_t paletteGroupsTableLength;
    uint32_t globalGfxAndPalettesOffset;
    uint32_t mapDataOffset;
    uint32_t areaRoomHeadersTableOffset;
    uint32_t areaTileSetsTableOffset;
    uint32_t areaRoomMapsTableOffset;
    uint32_t areaTableTableOffset;
    uint32_t areaTilesTableOffset;
    uint32_t spritePtrsTableOffset;
    uint32_t spritePtrsCount;
    uint32_t translationsTableOffset;
    uint8_t language;
    std::string variant;
    std::filesystem::path outputRoot;
};


std::vector<uint8_t> Rom;

bool load_rom(const std::filesystem::path& rom_path)
{
    std::ifstream file(rom_path, std::ios::binary);
    if (!file) {
        return false;
    }
    Rom = std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
    return true;
}

std::vector<uint8_t> extract_bytes(uint32_t offset, uint32_t length)
{
    if (offset + length > Rom.size()) {
        return {};
    }
    return std::vector<uint8_t>(Rom.begin() + offset, Rom.begin() + offset + length);
}

inline uint32_t to_gba_address(uint32_t offset)
{
    return 0x08000000 + offset;
}

inline uint32_t to_rom_address(uint32_t offset)
{
    return offset - 0x08000000;
}

inline uint32_t read_pointer(uint32_t offset)
{
    if (offset + 4 > Rom.size()) {
        return 0;
    }
    return Rom[offset] | (Rom[offset + 1] << 8) | (Rom[offset + 2] << 16) | (Rom[offset + 3] << 24);
}

inline bool lz77_uncompress(const std::vector<uint8_t>& compressed_data, std::vector<uint8_t>& uncompressed_data)
{
    if (compressed_data.size() < 4 || compressed_data[0] != 0x10) {
        return false;
    }

    const uint32_t decompressed_size =
        compressed_data[1] | (compressed_data[2] << 8) | (compressed_data[3] << 16);
    uncompressed_data.clear();
    uncompressed_data.reserve(decompressed_size);

    size_t src_pos = 4;
    while (src_pos < compressed_data.size() && uncompressed_data.size() < decompressed_size) {
        uint8_t flags = compressed_data[src_pos++];
        for (int i = 0; i < 8 && uncompressed_data.size() < decompressed_size; ++i) {
            if ((flags & 0x80) == 0) {
                if (src_pos >= compressed_data.size()) {
                    return false;
                }
                uncompressed_data.push_back(compressed_data[src_pos++]);
            } else {
                if (src_pos + 1 >= compressed_data.size()) {
                    return false;
                }

                const uint8_t first = compressed_data[src_pos++];
                const uint8_t second = compressed_data[src_pos++];
                size_t block_size = (first >> 4) + 3;
                const size_t block_distance = (((first & 0xF) << 8) | second) + 1;

                if (block_distance > uncompressed_data.size()) {
                    return false;
                }

                size_t block_pos = uncompressed_data.size() - block_distance;
                while (block_size-- > 0 && uncompressed_data.size() < decompressed_size) {
                    uncompressed_data.push_back(uncompressed_data[block_pos++]);
                }
            }
            flags <<= 1;
        }
    }

    return uncompressed_data.size() == decompressed_size;
}

inline bool json_asset_matches_variant(const nlohmann::json& asset, const std::string& variant)
{
    if (!asset.contains("variants")) {
        return true;
    }

    for (const auto& item : asset["variants"]) {
        if (item.get<std::string>() == variant) {
            return true;
        }
    }
    return false;
}

inline bool string_in_list(const std::string& value, const std::vector<std::string>& values)
{
    return std::find(values.begin(), values.end(), value) != values.end();
}

struct AssetRecord
{
    std::filesystem::path source_path;
    std::string type;
    uint32_t rom_start;
    uint32_t size;
    bool compressed;
};

inline bool path_has_suffix(const std::string& value, const std::string& suffix)
{
    return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline std::string text_category_name(uint32_t category)
{
    static const char* const kTextCategoryNames[] = {
        "TEXT_SAVE",           "TEXT_CREDITS",        "TEXT_NAMES",          "TEXT_NEWSLETTER",
        "TEXT_ITEMS",          "TEXT_ITEM_GET",       "TEXT_LOCATIONS",      "TEXT_WINDCRESTS",
        "TEXT_FIGURINE_NAMES", "TEXT_FIGURINE_DESCRIPTIONS",
        "TEXT_EMPTY",          "TEXT_EZLO",           "TEXT_EZLO2",          "TEXT_MINISH",
        "TEXT_KINSTONE",       "TEXT_PICORI",         "TEXT_PROLOGUE",       "TEXT_FINDING_EZLO",
        "TEXT_MINISH2",        "TEXT_VAATI",          "TEXT_GUSTAF",         "TEXT_PANEL_TUTORIAL",
        "TEXT_VAATI2",         "TEXT_GUSTAF2",        "TEXT_EMPTY2",         "TEXT_EMPTY3",
        "TEXT_FARMERS",        "TEXT_CARPENTERS",     "TEXT_EZLO_ELEMENTS_DONE",
        "TEXT_GORONS",         "TEXT_EMPTY4",         "TEXT_BELARI",         "TEXT_LON_LON",
        "TEXT_FOREST_MINISH",  "TEXT_EZLO_PORTAL",    "TEXT_PERCY",          "TEXT_BREAK_VAATI_CURSE",
        "TEXT_FESTIVAL",       "TEXT_EMPTY5",         "TEXT_TREASURE_GUARDIAN",
        "TEXT_DAMPE",          "TEXT_BUSINESS_SCRUB", "TEXT_EMPTY6",         "TEXT_PICOLYTE",
        "TEXT_STOCKWELL",      "TEXT_SYRUP",          "TEXT_ITEM_PRICES",    "TEXT_WIND_TRIBE",
        "TEXT_ANJU",           "TEXT_GORMAN_ORACLES", "TEXT_SMITH",          "TEXT_PHONOGRAPH",
        "TEXT_TOWN",           "TEXT_TOWN2",          "TEXT_TOWN3",          "TEXT_TOWN4",
        "TEXT_TOWN5",          "TEXT_TOWN6",          "TEXT_TOWN7",          "TEXT_MILK",
        "TEXT_BAKERY",         "TEXT_SIMON",          "TEXT_SCHOOL",         "TEXT_TINGLE",
        "TEXT_POST",           "TEXT_MUTOH",          "TEXT_BURLOV",         "TEXT_CARLOV",
        "TEXT_REM",            "TEXT_HAPPY_HEARTH",   "TEXT_BLADE_MASTERS",  "TEXT_ANSWER_HOUSE",
        "TEXT_UNK_WISE",       "TEXT_LIBRARY",        "TEXT_TOWN_MINISH1",   "TEXT_TOWN_MINISH2",
        "TEXT_HAGEN",          "TEXT_DR_LEFT",        "TEXT_TOWN8",          "TEXT_CAFE",
    };

    if (category < (sizeof(kTextCategoryNames) / sizeof(kTextCategoryNames[0]))) {
        return kTextCategoryNames[category];
    }

    return "TEXT_" + std::to_string(category);
}

inline std::string text_language_name(uint32_t language)
{
    return "language_" + std::to_string(language);
}

inline std::string text_language_display_name(const Config& config, uint32_t language)
{
    if (config.variant == "USA") {
        return "USA";
    }

    if (config.variant == "EU") {
        switch (language) {
            case 0:
            case 1:
            case 2:
                return "English";
            case 3:
                return "French";
            case 4:
                return "German";
            case 5:
                return "Spanish";
            case 6:
                return "Italian";
            default:
                break;
        }
    }

    return text_language_name(language);
}

inline std::string hex_byte_string(uint32_t value)
{
    std::ostringstream stream;
    stream << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << (value & 0xFF);
    return stream.str();
}

inline std::string text_message_preview(const std::vector<uint8_t>& bytes)
{
    std::string preview;
    for (uint8_t byte : bytes) {
        if (byte == 0) {
            break;
        }

        if (byte >= 0x20 && byte <= 0x7E) {
            preview.push_back(static_cast<char>(byte));
        } else if (byte == '\n') {
            preview += "\\n";
        } else if (byte == '\r') {
            preview += "\\r";
        } else if (byte == '\t') {
            preview += "\\t";
        } else {
            preview += "<" + hex_byte_string(byte) + ">";
        }
    }
    return preview;
}

inline std::vector<uint8_t> extract_text_message_bytes(uint32_t offset)
{
    std::vector<uint8_t> bytes;
    if (offset >= Rom.size()) {
        return bytes;
    }

    for (uint32_t at = offset; at < Rom.size(); ++at) {
        bytes.push_back(Rom[at]);
        if (Rom[at] == 0) {
            break;
        }
    }

    return bytes;
}

inline std::vector<AssetRecord> collect_embedded_asset_records(const Config& config,
                                                               const std::function<bool(const std::string&)>& predicate)
{
    std::vector<AssetRecord> records;
    const EmbeddedAssetEntry* asset_index = EmbeddedAssetIndex_Get();
    const u32 asset_count = EmbeddedAssetIndex_Count();

    for (u32 i = 0; i < asset_count; ++i) {
        const EmbeddedAssetEntry& entry = asset_index[i];
        const std::string path = entry.path;
        if (!predicate(path)) {
            continue;
        }

        const std::filesystem::path source_path = path;
        records.push_back({source_path, std::string(), entry.offset, entry.size, source_path.extension() == ".lz"});
    }

    return records;
}

inline std::string json_path_string(const std::filesystem::path& path)
{
    return path.generic_string();
}

struct ExtractedPaletteRecord
{
    AssetRecord asset;
    std::filesystem::path relative_output_path;
    uint32_t first_palette_id;
    uint32_t palette_count;
};

struct GfxExtractionResult
{
    std::filesystem::path relative_bmp_output;
    uint32_t output_size;
    uint16_t width;
    uint16_t height;
    uint8_t bpp;
};

inline bool write_binary_file(const std::filesystem::path& output_path, const std::vector<uint8_t>& data)
{
    std::filesystem::create_directories(output_path.parent_path());
    std::ofstream output(output_path, std::ios::binary);
    if (!output) {
        return false;
    }

    output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return true;
}

inline std::vector<uint8_t> gba_palette_to_rgba(const std::vector<uint8_t>& palette_data)
{
    std::vector<uint8_t> rgba;
    rgba.reserve((palette_data.size() / 2) * 4);

    for (size_t i = 0; i + 1 < palette_data.size(); i += 2) {
        const uint16_t color = palette_data[i] | (palette_data[i + 1] << 8);
        const uint8_t r = static_cast<uint8_t>(((color >> 0) & 0x1F) * 255 / 31);
        const uint8_t g = static_cast<uint8_t>(((color >> 5) & 0x1F) * 255 / 31);
        const uint8_t b = static_cast<uint8_t>(((color >> 10) & 0x1F) * 255 / 31);
        rgba.push_back(r);
        rgba.push_back(g);
        rgba.push_back(b);
        rgba.push_back(255);
    }

    return rgba;
}

std::vector<uint32_t> extract_gfx_group_addresses(uint32_t gfxGroupsTableOffset, uint32_t gfxGroupsTableLength)
{
    std::vector<uint32_t> gfx_group_addresses;
    for (uint32_t i = 1; i < gfxGroupsTableLength; i += 1) {
        uint32_t address = to_rom_address(read_pointer(gfxGroupsTableOffset + i*4));
        if (address != 0) {
            gfx_group_addresses.push_back(address);
            std::cout << "Found gfx group at index " << i << ": 0x" << std::hex << address << std::dec << std::endl;
        }
        else {
            std::cout << "Warning: Null pointer at gfx group index " << i << std::endl;
        }
    }
    return gfx_group_addresses;
}

std::vector<uint32_t> extract_palette_group_addresses(uint32_t paletteGroupsTableOffset, uint32_t paletteGroupsTableLength)
{
    std::vector<uint32_t> palette_group_addresses;
    for (uint32_t i = 1; i < paletteGroupsTableLength; i += 1) {
        const uint32_t raw_pointer = read_pointer(paletteGroupsTableOffset + i * 4);
        if (raw_pointer == 0) {
            std::cout << "Warning: Null pointer at palette group index " << i << std::endl;
            continue;
        }

        palette_group_addresses.push_back(to_rom_address(raw_pointer));
    }
    return palette_group_addresses;
}

struct PaletteGroupElement
{
    uint16_t paletteId;
    uint8_t destPaletteNum;
    uint8_t numPalettes;
    bool terminator;
};

typedef std::vector<PaletteGroupElement> PaletteGroup;

inline PaletteGroupElement parse_palette_group_element(const std::vector<uint8_t>& data, uint32_t offset)
{
    PaletteGroupElement element;
    element.paletteId = data[offset] | (data[offset + 1] << 8);
    element.destPaletteNum = data[offset + 2];
    element.numPalettes = data[offset + 3] & 0x0F;
    if (element.numPalettes == 0) {
        element.numPalettes = 16;
    }
    element.terminator = (data[offset + 3] & 0x80) == 0;
    return element;
}

inline PaletteGroup extract_palette_group(uint32_t palette_group_address)
{
    constexpr uint32_t kPaletteGroupEntrySize = 4;
    constexpr uint32_t kMaxEntriesPerGroup = 64;

    std::vector<uint8_t> palette_group_data =
        extract_bytes(palette_group_address, kPaletteGroupEntrySize * kMaxEntriesPerGroup);
    if (palette_group_data.size() < kPaletteGroupEntrySize) {
        std::cout << "Warning: palette group out of ROM range at 0x" << std::hex << palette_group_address << std::dec
                  << std::endl;
        return {};
    }

    PaletteGroup palette_group;
    for (uint32_t i = 0; i + kPaletteGroupEntrySize <= palette_group_data.size(); i += kPaletteGroupEntrySize) {
        PaletteGroupElement element = parse_palette_group_element(palette_group_data, i);
        palette_group.push_back(element);
        if (element.terminator) {
            break;
        }
    }

    return palette_group;
}

struct GfxGroupElement
{
    uint32_t src;
    uint32_t unknown;
    uint32_t dest;
    uint32_t size;
    bool compressed;
    bool terminator;
};

struct GfxGroupElement; 

typedef std::vector<GfxGroupElement> GfxGroup;

inline bool should_extract_gfx_element(const GfxGroupElement& element, uint8_t language)
{
    const uint8_t ctrl = element.unknown & 0x0F;
    switch (ctrl) {
        case 0x7:
            return true;
        case 0xD:
            return false;
        case 0xE:
            return language != 0 && language != 1;
        case 0xF:
            return language != 0;
        default:
            return ctrl == language;
    }
}

GfxGroupElement parse_gfx_group_element(const std::vector<uint8_t>& data, uint32_t offset)
{
    GfxGroupElement element;
    uint32_t raw0 = data[offset] | (data[offset + 1] << 8) | (data[offset + 2] << 16) | (data[offset + 3] << 24);
    uint32_t raw2 = data[offset + 8] | (data[offset + 9] << 8) | (data[offset + 10] << 16) | (data[offset + 11] << 24);
    element.src = raw0 & 0x00FFFFFF;
    element.unknown = (raw0 >> 24) & 0x7F;
    element.dest = data[offset + 4] | (data[offset + 5] << 8) | (data[offset + 6] << 16) | (data[offset + 7] << 24);
    element.size = raw2 & 0x7FFFFFFF;
    element.compressed = ((raw2 >> 31) & 0x1) != 0;
    element.terminator = ((raw0 >> 31) & 0x1) == 0;
    return element;
}
 
GfxGroup extract_gfx_group(uint32_t gfx_group_address)
{
    constexpr uint32_t kGfxGroupEntrySize = 12;
    constexpr uint32_t kMaxEntriesPerGroup = 256;

    std::vector<uint8_t> gfx_group_data = extract_bytes(gfx_group_address, kGfxGroupEntrySize * kMaxEntriesPerGroup);
    if (gfx_group_data.size() < kGfxGroupEntrySize) {
        std::cout << "Warning: gfx group out of ROM range at 0x" << std::hex << gfx_group_address << std::dec << std::endl;
        return GfxGroup();
    }

    GfxGroup gfx_group;
    for (uint32_t i = 0; i + kGfxGroupEntrySize <= gfx_group_data.size(); i += kGfxGroupEntrySize) {
        GfxGroupElement element = parse_gfx_group_element(gfx_group_data, i);
        gfx_group.push_back(element);

        if (element.terminator) {
            std::cout << "Reached terminator for gfx group at address 0x" << std::hex << gfx_group_address << std::dec << std::endl;
            break;
        }

        if (i + kGfxGroupEntrySize >= gfx_group_data.size()) {
            std::cout << "Warning: No terminator found for gfx group at 0x" << std::hex << gfx_group_address << std::dec << std::endl;
        }
    }
    return gfx_group;
}

inline bool bin_to_bmp(const std::vector<uint8_t>& gfx_data, const std::string& output_path, uint16_t width,
                       uint16_t height, uint8_t bpp = 4)
{
    std::string error;
    const std::vector<uint8_t> pixels = PortAssetPipeline::DecodeGbaTiledGfx(gfx_data, width, height, bpp);
    return !pixels.empty() && PortAssetPipeline::WriteIndexedBmp(output_path, pixels, width, height, bpp, &error);
}

struct GfxMetadata
{
    uint16_t width;
    uint16_t height;
    uint8_t bpp;       // 1, 2, 4, or 8 bits per pixel
    bool is_indexed;   // true for palette-based, false for truecolor
};

GfxMetadata detect_gfx_metadata(uint32_t src, uint32_t size, uint8_t bpp = 4)
{
    GfxMetadata meta;
    meta.bpp = bpp;
    meta.is_indexed = (bpp <= 8);
    
    // Validation: ensure bpp is valid
    if (bpp == 0 || bpp > 8) {
        std::cout << "Warning: Invalid bpp " << (int)bpp << " for gfx at 0x" << std::hex << src << ", using 4" << std::dec << std::endl;
        meta.bpp = 4;
    }
    
    const uint32_t bytes_per_tile = (meta.bpp == 8) ? 64 : 32;
    const uint32_t total_tiles = std::max<uint32_t>(1, size / bytes_per_tile);

    if (total_tiles == 0) {
        meta.width = 8;
        meta.height = 8;
        std::cout << "Warning: Empty/tiny gfx data at 0x" << std::hex << src << ", using 8x8" << std::dec << std::endl;
        return meta;
    }

    const uint32_t tiles_wide = std::max<uint32_t>(1, static_cast<uint32_t>(std::ceil(std::sqrt(total_tiles))));
    const uint32_t tiles_high = (total_tiles + tiles_wide - 1) / tiles_wide;
    meta.width = static_cast<uint16_t>(tiles_wide * 8);
    meta.height = static_cast<uint16_t>(tiles_high * 8);

    return meta;
}

std::optional<GfxExtractionResult> extract_gfx(uint32_t src, uint32_t size, bool compressed,
                                               uint32_t global_gfx_base_offset, uint8_t bpp = 4,
                                               uint16_t width = 0, uint16_t height = 0,
                                               const std::filesystem::path& output_root = "assets")
{
    const uint32_t rom_src = global_gfx_base_offset + src;
    std::vector<uint8_t> gfx_data = extract_bytes(rom_src, size);
    if (gfx_data.empty()) {
        std::cout << "Error: Failed to read gfx data at ROM offset 0x" << std::hex << rom_src << std::dec << std::endl;
        return std::nullopt;
    }
    if (compressed) {
        std::vector<uint8_t> uncompressed_data;
        if (!lz77_uncompress(gfx_data, uncompressed_data)) {
            std::cout << "Error: Failed to uncompress gfx data at 0x" << std::hex << src << " (ROM 0x" << rom_src << ")"
                      << std::dec << std::endl;
            return std::nullopt;
        }
        gfx_data = std::move(uncompressed_data);
    }
    
    // Auto-detect metadata if not provided
    GfxMetadata meta;
    if (width == 0 || height == 0) {
        meta = detect_gfx_metadata(src, gfx_data.size(), bpp);
    } else {
        meta.width = width;
        meta.height = height;
        meta.bpp = bpp;
        meta.is_indexed = (bpp <= 8);
    }
    
    // Create output folder
    std::filesystem::path gfx_folder = output_root / "gfx";
    if (!std::filesystem::exists(gfx_folder)) {
        std::filesystem::create_directories(gfx_folder);
    }
    
    // Write binary file with metadata in filename
    std::stringstream ss;
    ss << std::hex << src;
    std::string hex_src = ss.str();
    const std::filesystem::path relative_bmp_output = std::filesystem::path("gfx") /
        ("gfx_" + hex_src + "_" + std::to_string(meta.width) + "x" + std::to_string(meta.height) + "_" +
         std::to_string(meta.bpp) + "bpp" + (compressed ? "_compressed" : "_uncompressed") + ".bmp");
    std::filesystem::path bmp_output = output_root / relative_bmp_output;
    if (!bin_to_bmp(gfx_data, bmp_output.string(), meta.width, meta.height, meta.bpp)) {
        std::cout << "Error: Failed to generate editable BMP for gfx at 0x" << std::hex << src << std::dec << std::endl;
        return std::nullopt;
    }

    std::cout << "Extracted gfx: src=0x" << std::hex << src << " rom=0x" << rom_src << std::dec << " " << meta.width
              << "x" << meta.height << " @ " << (int)meta.bpp << "bpp to " << bmp_output << std::endl;

    return GfxExtractionResult{relative_bmp_output, static_cast<uint32_t>(gfx_data.size()), meta.width, meta.height,
                               meta.bpp};
}



inline bool extract_all_gfx(uint32_t gfx_group_address, uint32_t gfx_group_table_length, uint32_t global_gfx_base_offset,
                            uint8_t language, const std::filesystem::path& output_root)
{
    std::vector<uint32_t> gfx_group_addresses = extract_gfx_group_addresses(gfx_group_address, gfx_group_table_length);
    std::cout << "Found " << gfx_group_addresses.size() << " gfx groups." << std::endl;
    std::vector<GfxGroup> gfx_groups;
    for (uint32_t address : gfx_group_addresses) {
        GfxGroup gfx_group = extract_gfx_group(address);
        gfx_groups.push_back(gfx_group);
    }

    std::cout << "Extracting gfx and writing gfx_groups.json..." << std::endl;
    nlohmann::json json_gfx_groups;
    for (size_t i = 0; i < gfx_groups.size(); ++i) {
        const GfxGroup& group = gfx_groups[i];
        nlohmann::json json_group;
        for (const auto& element : group) {
            nlohmann::json json_element;
            json_element["unknown"] = element.unknown;
            json_element["dest"] = element.dest;
            json_element["compressed"] = element.compressed;
            json_element["terminator"] = element.terminator;

            if (should_extract_gfx_element(element, language)) {
                const std::optional<GfxExtractionResult> extracted =
                    extract_gfx(element.src, element.size, element.compressed, global_gfx_base_offset, 4, 0, 0,
                                output_root);
                if (extracted.has_value()) {
                    json_element["file"] = json_path_string(extracted->relative_bmp_output);
                    json_element["size"] = extracted->output_size;
                    json_element["width"] = extracted->width;
                    json_element["height"] = extracted->height;
                    json_element["bpp"] = extracted->bpp;
                } else {
                    std::cout << "Error: Failed to extract gfx data for element with src 0x" << std::hex << element.src
                              << std::dec << std::endl;
                }
            } else {
                json_element["file"] = nullptr;
                json_element["size"] = 0;
            }

            json_group.push_back(json_element);
        }
        json_gfx_groups[std::to_string(i + 1)] = json_group;
    }
    std::ofstream json_file(output_root / "gfx_groups.json");
    json_file << json_gfx_groups.dump(4);
    std::cout << "Finished writing gfx groups to JSON file." << std::endl;
    return true;
}

inline nlohmann::json build_palette_file_refs(const std::vector<ExtractedPaletteRecord>& palette_records,
                                              uint32_t palette_id, uint32_t num_palettes)
{
    nlohmann::json refs = nlohmann::json::array();
    uint32_t next_palette_id = palette_id;
    uint32_t remaining_palettes = num_palettes;

    while (remaining_palettes > 0) {
        bool found_segment = false;
        for (const ExtractedPaletteRecord& record : palette_records) {
            const uint32_t record_end = record.first_palette_id + record.palette_count;
            if (next_palette_id < record.first_palette_id || next_palette_id >= record_end) {
                continue;
            }

            const uint32_t palette_offset = next_palette_id - record.first_palette_id;
            const uint32_t palettes_in_segment =
                std::min<uint32_t>(remaining_palettes, record.palette_count - palette_offset);

            nlohmann::json ref;
            ref["asset"] = json_path_string(record.asset.source_path);
            ref["file"] = json_path_string(record.relative_output_path);
            ref["palette_offset"] = palette_offset;
            ref["num_palettes"] = palettes_in_segment;
            ref["byte_offset"] = palette_offset * 32;
            ref["size"] = palettes_in_segment * 32;
            refs.push_back(ref);

            next_palette_id += palettes_in_segment;
            remaining_palettes -= palettes_in_segment;
            found_segment = true;
            break;
        }

        if (!found_segment) {
            break;
        }
    }

    return refs;
}

inline std::vector<ExtractedPaletteRecord> extract_all_palettes(const Config& config)
{
    const std::vector<AssetRecord> palette_records = collect_embedded_asset_records(
        config, [](const std::string& path) { return path.rfind("palettes/", 0) == 0 && !path.empty(); });
    std::cout << "Found " << palette_records.size() << " palette assets." << std::endl;

    std::vector<ExtractedPaletteRecord> extracted_palettes;
    nlohmann::json json_palettes = nlohmann::json::array();
    for (const AssetRecord& record : palette_records) {
        const std::vector<uint8_t> palette_data = extract_bytes(record.rom_start, record.size);
        if (palette_data.empty()) {
            std::cout << "Warning: Failed to read palette at ROM offset 0x" << std::hex << record.rom_start << std::dec
                      << std::endl;
            continue;
        }

        std::filesystem::path output_path = config.outputRoot / "palettes" / record.source_path.filename();
        output_path.replace_extension(".json");
        std::string palette_error;
        if (!PortAssetPipeline::WritePaletteJson(output_path, palette_data, &palette_error)) {
            std::cout << "Warning: Failed to write palette " << output_path << std::endl;
            continue;
        }

        std::filesystem::path relative_output_path = std::filesystem::path("palettes") / record.source_path.filename();
        relative_output_path.replace_extension(".json");
        const uint32_t first_palette_id =
            (record.rom_start >= config.globalGfxAndPalettesOffset)
                ? ((record.rom_start - config.globalGfxAndPalettesOffset) / 32)
                : 0;
        const uint32_t palette_count = record.size / 32;
        extracted_palettes.push_back({record, relative_output_path, first_palette_id, palette_count});

        nlohmann::json json_palette;
        json_palette["asset"] = json_path_string(record.source_path);
        json_palette["file"] = json_path_string(relative_output_path);
        json_palette["size"] = record.size;
        if (record.size % 32 == 0 && record.rom_start >= config.globalGfxAndPalettesOffset) {
            json_palette["palette_id"] = first_palette_id;
            json_palette["num_palettes"] = palette_count;
        }
        json_palettes.push_back(json_palette);
    }

    std::ofstream json_file(config.outputRoot / "palettes.json");
    json_file << json_palettes.dump(4);
    std::cout << "Finished writing palettes.json" << std::endl;
    return extracted_palettes;
}

inline bool extract_all_palette_groups(const Config& config, const std::vector<ExtractedPaletteRecord>& palette_records)
{
    const std::vector<uint32_t> palette_group_addresses =
        extract_palette_group_addresses(config.paletteGroupsTableOffset, config.paletteGroupsTableLength);
    std::cout << "Found " << palette_group_addresses.size() << " palette groups." << std::endl;

    nlohmann::json json_palette_groups;
    for (size_t i = 0; i < palette_group_addresses.size(); ++i) {
        const uint32_t group_index = static_cast<uint32_t>(i + 1);
        const PaletteGroup group = extract_palette_group(palette_group_addresses[i]);

        nlohmann::json json_group;
        for (const PaletteGroupElement& element : group) {
            nlohmann::json json_element;
            json_element["palette_id"] = element.paletteId;
            json_element["dest_palette_num"] = element.destPaletteNum;
            json_element["num_palettes"] = element.numPalettes;
            json_element["terminator"] = element.terminator;
            json_element["palette_files"] =
                build_palette_file_refs(palette_records, element.paletteId, element.numPalettes);
            json_element["size"] = element.numPalettes * 32;
            json_group.push_back(json_element);
        }

        nlohmann::json json_group_root;
        json_group_root["entries"] = json_group;
        json_palette_groups[std::to_string(group_index)] = json_group_root;
    }

    std::ofstream json_file(config.outputRoot / "palette_groups.json");
    json_file << json_palette_groups.dump(4);
    std::cout << "Finished writing palette_groups.json" << std::endl;
    return true;
}

inline bool extract_all_tilemaps(const Config& config)
{
    const std::vector<AssetRecord> tilemap_records = collect_embedded_asset_records(config, [](const std::string& path) {
        return (path.find("/rooms/") != std::string::npos && path_has_suffix(path, ".bin.lz")) ||
               path.rfind("assets/gAreaRoomMap_", 0) == 0;
    });
    std::cout << "Found " << tilemap_records.size() << " tilemaps." << std::endl;

    nlohmann::json json_tilemaps = nlohmann::json::array();
    for (const AssetRecord& record : tilemap_records) {
        const std::vector<uint8_t> tilemap_data = extract_bytes(record.rom_start, record.size);
        if (tilemap_data.empty()) {
            std::cout << "Warning: Failed to read tilemap at ROM offset 0x" << std::hex << record.rom_start << std::dec
                      << std::endl;
            continue;
        }

        const std::filesystem::path raw_output = config.outputRoot / "tilemaps" / record.source_path;
        if (!write_binary_file(raw_output, tilemap_data)) {
            std::cout << "Warning: Failed to write tilemap " << raw_output << std::endl;
            continue;
        }

        const std::filesystem::path relative_raw_output = std::filesystem::path("tilemaps") / record.source_path;
        nlohmann::json json_tilemap;
        json_tilemap["asset"] = json_path_string(record.source_path);
        json_tilemap["file"] = json_path_string(relative_raw_output);
        json_tilemap["size"] = record.size;
        json_tilemap["compressed"] = record.compressed;

        if (record.compressed) {
            std::vector<uint8_t> decompressed_data;
            if (lz77_uncompress(tilemap_data, decompressed_data)) {
                std::filesystem::path decompressed_output = raw_output;
                decompressed_output.replace_extension("");
                if (write_binary_file(decompressed_output, decompressed_data)) {
                    std::filesystem::path relative_decompressed_output = relative_raw_output;
                    relative_decompressed_output.replace_extension("");
                    json_tilemap["decompressed_file"] = json_path_string(relative_decompressed_output);
                    json_tilemap["decompressed_size"] = decompressed_data.size();
                }
            } else {
                std::cout << "Warning: Failed to uncompress tilemap " << record.source_path << std::endl;
            }
        }

        json_tilemaps.push_back(json_tilemap);
    }

    std::ofstream json_file(config.outputRoot / "tilemaps.json");
    json_file << json_tilemaps.dump(4);
    std::cout << "Finished writing tilemaps.json" << std::endl;
    return true;
}

struct IndexedAssetInfo
{
    std::filesystem::path path;
    uint32_t size;
};

inline std::unordered_map<uint32_t, IndexedAssetInfo> build_embedded_asset_lookup()
{
    std::unordered_map<uint32_t, IndexedAssetInfo> lookup;
    const EmbeddedAssetEntry* asset_index = EmbeddedAssetIndex_Get();
    const u32 asset_count = EmbeddedAssetIndex_Count();
    for (u32 i = 0; i < asset_count; ++i) {
        lookup.emplace(asset_index[i].offset, IndexedAssetInfo{asset_index[i].path, asset_index[i].size});
    }
    return lookup;
}

inline bool is_valid_rom_pointer(uint32_t value)
{
    return value >= 0x08000000u && value < 0x08000000u + Rom.size();
}

inline bool is_valid_table_value(uint32_t value)
{
    return value == 0 || is_valid_rom_pointer(value);
}

inline uint32_t scan_pointer_table_count(uint32_t table_offset, uint32_t max_entries)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < max_entries; ++i) {
        const uint32_t value = read_pointer(table_offset + i * 4);
        if (is_valid_table_value(value)) {
            count = i + 1;
        } else {
            break;
        }
    }
    return count;
}

inline std::string hex_offset_string(uint32_t offset)
{
    std::stringstream ss;
    ss << std::hex << offset;
    return ss.str();
}

inline std::vector<uint32_t> build_inference_boundaries(const std::unordered_map<uint32_t, IndexedAssetInfo>& lookup,
                                                        const std::vector<uint32_t>& extra_offsets)
{
    std::set<uint32_t> ordered_offsets;
    for (const auto& item : lookup) {
        ordered_offsets.insert(item.first);
    }
    for (uint32_t offset : extra_offsets) {
        if (offset < Rom.size()) {
            ordered_offsets.insert(offset);
        }
    }
    ordered_offsets.insert(static_cast<uint32_t>(Rom.size()));
    return std::vector<uint32_t>(ordered_offsets.begin(), ordered_offsets.end());
}

inline uint32_t infer_asset_size(uint32_t offset, const std::unordered_map<uint32_t, IndexedAssetInfo>& lookup,
                                 const std::vector<uint32_t>& boundaries, uint32_t fallback_size = 4)
{
    const auto exact = lookup.find(offset);
    if (exact != lookup.end()) {
        return exact->second.size;
    }

    const auto next = std::upper_bound(boundaries.begin(), boundaries.end(), offset);
    if (next != boundaries.end() && *next > offset) {
        return *next - offset;
    }

    if (offset + fallback_size <= Rom.size()) {
        return fallback_size;
    }

    return offset < Rom.size() ? static_cast<uint32_t>(Rom.size()) - offset : 0;
}

inline std::filesystem::path extract_asset_or_raw(uint32_t rom_offset, uint32_t size,
                                                  const std::unordered_map<uint32_t, IndexedAssetInfo>& lookup,
                                                  const std::filesystem::path& output_root,
                                                  const std::filesystem::path& generated_prefix)
{
    auto found = lookup.find(rom_offset);
    std::filesystem::path relative_path;
    if (found != lookup.end()) {
        relative_path = found->second.path;
        size = found->second.size;
    } else {
        relative_path = generated_prefix / ("offset_" + hex_offset_string(rom_offset) + ".bin");
    }

    const std::vector<uint8_t> data = extract_bytes(rom_offset, size);
    write_binary_file(output_root / relative_path, data);
    return relative_path;
}

inline nlohmann::json extract_map_definition_sequence(
    uint32_t sequence_offset, const Config& config, const std::unordered_map<uint32_t, IndexedAssetInfo>& lookup,
    const std::vector<uint32_t>& boundaries, const std::filesystem::path& generated_prefix)
{
    constexpr uint32_t kMapMultiple = 0x80000000u;
    constexpr uint32_t kMapCompressed = 0x80000000u;
    constexpr uint32_t kEntrySize = 12;

    nlohmann::json json_sequence = nlohmann::json::array();
    uint32_t entry_offset = sequence_offset;
    while (entry_offset + kEntrySize <= Rom.size()) {
        const uint32_t src = read_pointer(entry_offset + 0);
        const uint32_t dest = read_pointer(entry_offset + 4);
        const uint32_t size = read_pointer(entry_offset + 8);
        const bool multiple = (src & kMapMultiple) != 0;
        nlohmann::json json_entry;
        json_entry["multiple"] = multiple;

        if (dest == 0) {
            json_entry["palette_group"] = src & 0xFFFF;
        } else {
            const bool compressed = (size & kMapCompressed) != 0;
            const uint32_t data_offset = config.mapDataOffset + (src & 0x7FFFFFFF);
            const uint32_t data_size = size & 0x7FFFFFFF;
            const uint32_t file_size = compressed ? infer_asset_size(data_offset, lookup, boundaries, data_size) : data_size;
            const std::filesystem::path relative_path =
                extract_asset_or_raw(data_offset, file_size, lookup, config.outputRoot, generated_prefix);
            json_entry["file"] = json_path_string(relative_path);
            json_entry["dest"] = dest;
            json_entry["size"] = data_size;
            json_entry["compressed"] = compressed;
        }

        json_sequence.push_back(json_entry);
        entry_offset += kEntrySize;
        if (!multiple) {
            break;
        }
    }

    return json_sequence;
}

inline std::vector<uint32_t> collect_area_table_data_offsets(const Config& config)
{
    std::vector<uint32_t> offsets;
    for (uint32_t area = 0; area < 0x90; ++area) {
        const uint32_t area_table_ptr = read_pointer(config.areaTableTableOffset + area * 4);
        if (!is_valid_rom_pointer(area_table_ptr)) {
            continue;
        }

        const uint32_t room_table_offset = to_rom_address(area_table_ptr);
        const uint32_t room_count = scan_pointer_table_count(room_table_offset, 64);
        for (uint32_t room = 0; room < room_count; ++room) {
            const uint32_t room_ptr = read_pointer(room_table_offset + room * 4);
            if (!is_valid_rom_pointer(room_ptr)) {
                continue;
            }

            const uint32_t room_offset = to_rom_address(room_ptr);
            const uint32_t property_count = scan_pointer_table_count(room_offset, 64);
            for (uint32_t idx = 0; idx < property_count; ++idx) {
                const uint32_t value = read_pointer(room_offset + idx * 4);
                if (!is_valid_table_value(value)) {
                    break;
                }
                if ((idx >= 4 && idx <= 7) || !is_valid_rom_pointer(value)) {
                    continue;
                }
                offsets.push_back(to_rom_address(value));
            }
        }
    }
    return offsets;
}

inline std::vector<uint32_t> collect_sprite_animation_table_offsets(const Config& config)
{
    std::vector<uint32_t> offsets;
    for (uint32_t i = 0; i < config.spritePtrsCount; ++i) {
        const uint32_t animations_ptr = read_pointer(config.spritePtrsTableOffset + i * 16);
        if (is_valid_rom_pointer(animations_ptr) && (animations_ptr & 1) == 0) {
            offsets.push_back(to_rom_address(animations_ptr));
        }
    }
    return offsets;
}

inline bool extract_area_room_headers(const Config& config)
{
    nlohmann::json json_headers = nlohmann::json::object();
    for (uint32_t area = 0; area < 0x90; ++area) {
        const uint32_t room_headers_ptr = read_pointer(config.areaRoomHeadersTableOffset + area * 4);
        nlohmann::json json_area = nlohmann::json::array();
        if (is_valid_rom_pointer(room_headers_ptr)) {
            uint32_t room_offset = to_rom_address(room_headers_ptr);
            for (uint32_t room = 0; room < 64 && room_offset + 2 <= Rom.size(); ++room) {
                const uint16_t sentinel = Rom[room_offset + 0] | (Rom[room_offset + 1] << 8);
                if (sentinel == 0xFFFF) {
                    break;
                }

                if (room_offset + 10 > Rom.size()) {
                    break;
                }

                nlohmann::json json_room;
                json_room["map_x"] = Rom[room_offset + 0] | (Rom[room_offset + 1] << 8);
                json_room["map_y"] = Rom[room_offset + 2] | (Rom[room_offset + 3] << 8);
                json_room["pixel_width"] = Rom[room_offset + 4] | (Rom[room_offset + 5] << 8);
                json_room["pixel_height"] = Rom[room_offset + 6] | (Rom[room_offset + 7] << 8);
                json_room["tile_set_id"] = Rom[room_offset + 8] | (Rom[room_offset + 9] << 8);
                json_area.push_back(json_room);
                room_offset += 10;
            }
        }
        json_headers[std::to_string(area)] = json_area;
    }

    std::ofstream json_file(config.outputRoot / "area_room_headers.json");
    json_file << json_headers.dump(4);
    std::cout << "Finished writing area_room_headers.json" << std::endl;
    return true;
}

inline bool extract_area_map_table(const Config& config, uint32_t table_offset,
                                   const std::unordered_map<uint32_t, IndexedAssetInfo>& lookup,
                                   const std::vector<uint32_t>& boundaries, const char* json_name,
                                   const std::filesystem::path& generated_prefix, bool direct_sequences)
{
    nlohmann::json json_root = nlohmann::json::object();

    for (uint32_t area = 0; area < 0x90; ++area) {
        const uint32_t area_ptr = read_pointer(table_offset + area * 4);
        nlohmann::json json_area = nlohmann::json::array();

        if (is_valid_rom_pointer(area_ptr)) {
            if (direct_sequences) {
                json_area = extract_map_definition_sequence(to_rom_address(area_ptr), config, lookup, boundaries,
                                                            generated_prefix);
            } else {
                const uint32_t subtable_offset = to_rom_address(area_ptr);
                const uint32_t count = scan_pointer_table_count(subtable_offset, 64);
                for (uint32_t i = 0; i < count; ++i) {
                    const uint32_t seq_ptr = read_pointer(subtable_offset + i * 4);
                    if (is_valid_rom_pointer(seq_ptr)) {
                        json_area.push_back(extract_map_definition_sequence(to_rom_address(seq_ptr), config, lookup,
                                                                            boundaries, generated_prefix));
                    } else {
                        json_area.push_back(nlohmann::json::array());
                    }
                }
            }
        }

        json_root[std::to_string(area)] = json_area;
    }

    std::ofstream json_file(config.outputRoot / json_name);
    json_file << json_root.dump(4);
    std::cout << "Finished writing " << json_name << std::endl;
    return true;
}

inline bool extract_area_tables(const Config& config, const std::unordered_map<uint32_t, IndexedAssetInfo>& lookup,
                                const std::vector<uint32_t>& boundaries)
{
    nlohmann::json json_root = nlohmann::json::object();

    for (uint32_t area = 0; area < 0x90; ++area) {
        const uint32_t area_ptr = read_pointer(config.areaTableTableOffset + area * 4);
        nlohmann::json json_area = nlohmann::json::array();

        if (is_valid_rom_pointer(area_ptr)) {
            const uint32_t subtable_offset = to_rom_address(area_ptr);
            const uint32_t count = scan_pointer_table_count(subtable_offset, 64);

            for (uint32_t room = 0; room < count; ++room) {
                const uint32_t room_ptr = read_pointer(subtable_offset + room * 4);
                nlohmann::json json_room = nlohmann::json::array();
                if (is_valid_rom_pointer(room_ptr)) {
                    const uint32_t room_offset = to_rom_address(room_ptr);
                    const uint32_t property_count = scan_pointer_table_count(room_offset, 64);
                    for (uint32_t idx = 0; idx < property_count; ++idx) {
                        const uint32_t value = read_pointer(room_offset + idx * 4);
                        if (!is_valid_table_value(value)) {
                            break;
                        }

                        if (value == 0 || !is_valid_rom_pointer(value)) {
                            json_room.push_back(nullptr);
                            continue;
                        }

                        const uint32_t data_offset = to_rom_address(value);

                        /* Properties at idx 4..7 are usually room callback functions
                         * that the port resolves via Port_GetRoomFuncProp. But some
                         * rooms (e.g. Minish Forest lily pads, doorway data tables)
                         * put real data pointers there. The asset index distinguishes
                         * code from data: if the ROM offset has an indexed asset entry
                         * (e.g. data_080D5360/...), treat it as data and extract it.
                         * Otherwise it's code — skip. */
                        if (idx >= 4 && idx <= 7) {
                            if (lookup.find(data_offset) == lookup.end()) {
                                json_room.push_back(nullptr);
                                continue;
                            }
                        }

                        const uint32_t data_size = infer_asset_size(data_offset, lookup, boundaries, 4);
                        const std::filesystem::path relative_path = extract_asset_or_raw(
                            data_offset, data_size, lookup, config.outputRoot, "room_properties");
                        json_room.push_back(json_path_string(relative_path));
                    }
                }
                json_area.push_back(json_room);
            }
        }

        json_root[std::to_string(area)] = json_area;
    }

    std::ofstream json_file(config.outputRoot / "area_tables.json");
    json_file << json_root.dump(4);
    std::cout << "Finished writing area_tables.json" << std::endl;
    return true;
}

inline bool extract_sprite_ptrs(const Config& config, const std::unordered_map<uint32_t, IndexedAssetInfo>& lookup,
                                const std::vector<uint32_t>& boundaries)
{
    std::unordered_map<std::string, std::filesystem::path> editable_animation_paths;
    std::unordered_map<std::string, std::filesystem::path> editable_frame_paths;
    std::unordered_map<std::string, std::filesystem::path> editable_ptr_paths;
    nlohmann::json json_root = nlohmann::json::array();

    auto source_path_for = [&](uint32_t rom_offset, const std::filesystem::path& generated_prefix) {
        const auto found = lookup.find(rom_offset);
        if (found != lookup.end()) {
            return found->second.path;
        }
        return generated_prefix / ("offset_" + hex_offset_string(rom_offset) + ".bin");
    };

    auto convert_animation = [&](uint32_t rom_offset, uint32_t size) -> std::filesystem::path {
        const std::filesystem::path raw_relative_path = source_path_for(rom_offset, "generated/animations");
        const std::string cache_key = raw_relative_path.generic_string();
        const auto found = editable_animation_paths.find(cache_key);
        if (found != editable_animation_paths.end()) {
            return found->second;
        }

        const std::vector<uint8_t> data = extract_bytes(rom_offset, size);
        std::filesystem::path editable_path = raw_relative_path;
        editable_path.replace_extension(".json");
        std::error_code ec;
        std::filesystem::remove(config.outputRoot / raw_relative_path, ec);

        std::string write_error;
        if (!PortAssetPipeline::WriteEditableAnimation(config.outputRoot / editable_path, data, &write_error)) {
            std::cout << "Warning: Failed to write editable animation " << editable_path << ": " << write_error
                      << std::endl;
            write_binary_file(config.outputRoot / raw_relative_path, data);
            editable_animation_paths[cache_key] = raw_relative_path;
            return raw_relative_path;
        }

        editable_animation_paths[cache_key] = editable_path;
        return editable_path;
    };

    auto convert_frames = [&](uint32_t rom_offset, uint32_t size) -> std::filesystem::path {
        const std::filesystem::path raw_relative_path = source_path_for(rom_offset, "generated/sprites");
        const std::string cache_key = raw_relative_path.generic_string();
        const auto found = editable_frame_paths.find(cache_key);
        if (found != editable_frame_paths.end()) {
            return found->second;
        }

        const std::vector<uint8_t> data = extract_bytes(rom_offset, size);
        std::filesystem::path editable_path = raw_relative_path;
        editable_path.replace_extension(".json");
        std::error_code ec;
        std::filesystem::remove(config.outputRoot / raw_relative_path, ec);

        std::string write_error;
        if (!PortAssetPipeline::WriteEditableSpriteFrames(config.outputRoot / editable_path, data, &write_error)) {
            std::cout << "Warning: Failed to write editable sprite frames " << editable_path << ": " << write_error
                      << std::endl;
            write_binary_file(config.outputRoot / raw_relative_path, data);
            editable_frame_paths[cache_key] = raw_relative_path;
            return raw_relative_path;
        }

        editable_frame_paths[cache_key] = editable_path;
        return editable_path;
    };

    auto convert_ptr_gfx = [&](uint32_t rom_offset, uint32_t size, nlohmann::json& json_entry) -> std::filesystem::path {
        const std::filesystem::path raw_relative_path = source_path_for(rom_offset, "generated/sprites");
        if (raw_relative_path.extension() != ".4bpp") {
            const std::vector<uint8_t> data = extract_bytes(rom_offset, size);
            write_binary_file(config.outputRoot / raw_relative_path, data);
            return raw_relative_path;
        }

        const GfxMetadata meta = detect_gfx_metadata(rom_offset, size, 4);
        json_entry["ptr_runtime_file"] = raw_relative_path.generic_string();
        json_entry["ptr_width"] = meta.width;
        json_entry["ptr_height"] = meta.height;
        json_entry["ptr_bpp"] = meta.bpp;
        json_entry["ptr_size"] = size;

        const std::string cache_key = raw_relative_path.generic_string();
        const auto found = editable_ptr_paths.find(cache_key);
        if (found != editable_ptr_paths.end()) {
            return found->second;
        }

        const std::vector<uint8_t> data = extract_bytes(rom_offset, size);
        const std::vector<uint8_t> pixels =
            PortAssetPipeline::DecodeGbaTiledGfx(data, meta.width, meta.height, meta.bpp);

        std::filesystem::path editable_path = raw_relative_path;
        editable_path.replace_extension(".bmp");
        std::error_code ec;
        std::filesystem::remove(config.outputRoot / raw_relative_path, ec);

        std::string write_error;
        if (pixels.empty() ||
            !PortAssetPipeline::WriteIndexedBmp(config.outputRoot / editable_path, pixels, meta.width, meta.height,
                                                meta.bpp, &write_error)) {
            std::cout << "Warning: Failed to write editable sprite gfx " << editable_path << ": " << write_error
                      << std::endl;
            write_binary_file(config.outputRoot / raw_relative_path, data);
            editable_ptr_paths[cache_key] = raw_relative_path;
            return raw_relative_path;
        }

        editable_ptr_paths[cache_key] = editable_path;
        return editable_path;
    };

    for (uint32_t i = 0; i < config.spritePtrsCount; ++i) {
        const uint32_t base = config.spritePtrsTableOffset + i * 16;
        const uint32_t animations_ptr = read_pointer(base + 0);
        const uint32_t frames_ptr = read_pointer(base + 4);
        const uint32_t ptr_ptr = read_pointer(base + 8);
        const uint32_t pad = read_pointer(base + 12);

        nlohmann::json json_entry;
        json_entry["animations"] = nlohmann::json::array();
        json_entry["frames_file"] = nullptr;
        json_entry["ptr_file"] = nullptr;
        json_entry["pad"] = pad;

        if (is_valid_rom_pointer(animations_ptr) && (animations_ptr & 1) == 0) {
            const uint32_t table_offset = to_rom_address(animations_ptr);
            const uint32_t table_size = infer_asset_size(table_offset, lookup, boundaries, 4);
            const uint32_t max_count = std::min<uint32_t>(256, table_size / 4);
            for (uint32_t anim = 0; anim < max_count; ++anim) {
                const uint32_t anim_ptr = read_pointer(table_offset + anim * 4);
                if (!is_valid_rom_pointer(anim_ptr)) {
                    break;
                }

                const uint32_t anim_offset = to_rom_address(anim_ptr);
                const uint32_t anim_size = infer_asset_size(anim_offset, lookup, boundaries, 4);
                const std::filesystem::path relative_path = convert_animation(anim_offset, anim_size);
                json_entry["animations"].push_back(json_path_string(relative_path));
            }
        }

        if (is_valid_rom_pointer(frames_ptr) && (frames_ptr & 1) == 0) {
            const uint32_t frames_offset = to_rom_address(frames_ptr);
            const uint32_t frames_size = infer_asset_size(frames_offset, lookup, boundaries, 4);
            const std::filesystem::path relative_path = convert_frames(frames_offset, frames_size);
            json_entry["frames_file"] = json_path_string(relative_path);
        }

        if (is_valid_rom_pointer(ptr_ptr) && (ptr_ptr & 1) == 0) {
            const uint32_t ptr_offset = to_rom_address(ptr_ptr);
            const uint32_t ptr_size = infer_asset_size(ptr_offset, lookup, boundaries, 4);
            const std::filesystem::path relative_path = convert_ptr_gfx(ptr_offset, ptr_size, json_entry);
            json_entry["ptr_file"] = json_path_string(relative_path);
        }

        json_root.push_back(json_entry);
    }

    std::ofstream json_file(config.outputRoot / "sprite_ptrs.json");
    json_file << json_root.dump(4);
    std::cout << "Finished writing sprite_ptrs.json" << std::endl;
    return true;
}

inline bool extract_texts(const Config& config)
{
    nlohmann::json json_root = nlohmann::json::object();
    std::error_code ec;
    std::filesystem::remove_all(config.outputRoot / "texts", ec);

    struct LanguageGroup {
        uint32_t representative_language;
        uint32_t root_ptr;
        std::vector<uint32_t> engine_slots;
    };

    std::vector<LanguageGroup> groups;
    std::unordered_map<uint32_t, size_t> group_by_root;
    for (uint32_t language = 0; language < 7; ++language) {
        const uint32_t root_ptr = read_pointer(config.translationsTableOffset + language * 4);
        auto found = group_by_root.find(root_ptr);
        if (found == group_by_root.end()) {
            LanguageGroup group;
            group.representative_language = language;
            group.root_ptr = root_ptr;
            group.engine_slots.push_back(language);
            group_by_root.emplace(root_ptr, groups.size());
            groups.push_back(std::move(group));
        } else {
            groups[found->second].engine_slots.push_back(language);
        }
    }

    for (const LanguageGroup& group : groups) {
        const uint32_t language = group.representative_language;
        const uint32_t root_ptr = group.root_ptr;
        nlohmann::json json_language;
        const std::string language_name = text_language_display_name(config, language);
        json_language["name"] = language_name;
        json_language["engine_slots"] = nlohmann::json::array();
        for (uint32_t slot : group.engine_slots) {
            json_language["engine_slots"].push_back(slot);
        }

        if (root_ptr < 0x08000000u || root_ptr >= 0x08000000u + Rom.size()) {
            json_language["valid"] = false;
            json_root[std::to_string(language)] = json_language;
            continue;
        }

        const uint32_t root_offset = to_rom_address(root_ptr);
        if (root_offset + 4 > Rom.size()) {
            json_language["valid"] = false;
            json_root[std::to_string(language)] = json_language;
            continue;
        }

        const uint32_t category_count = read_pointer(root_offset) / 4;
        json_language["valid"] = true;
        json_language["table_rom_offset"] = root_offset;
        json_language["category_count"] = category_count;

        nlohmann::json json_categories = nlohmann::json::object();
        for (uint32_t category = 0; category < category_count; ++category) {
            const uint32_t category_table_relative = read_pointer(root_offset + category * 4);
            if (category_table_relative == 0) {
                continue;
            }

            const uint32_t category_table_offset = root_offset + category_table_relative;
            if (category_table_offset + 4 > Rom.size()) {
                continue;
            }

            const uint32_t message_count = read_pointer(category_table_offset) / 4;
            nlohmann::json json_category;
            json_category["name"] = text_category_name(category);
            json_category["table_rom_offset"] = category_table_offset;
            json_category["message_count"] = message_count;

            nlohmann::json json_messages = nlohmann::json::array();
            for (uint32_t message = 0; message < message_count; ++message) {
                const uint32_t message_relative = read_pointer(category_table_offset + message * 4);
                const uint32_t message_offset = category_table_offset + message_relative;
                if (message_relative == 0 || message_offset >= Rom.size()) {
                    continue;
                }

                std::string symbolic_text;
                size_t consumed_bytes = 0;
                std::string text_error;
                if (!PortAssetPipeline::DecodeTmcText(Rom.data() + message_offset, Rom.size() - message_offset,
                                                     symbolic_text, &consumed_bytes, &text_error)) {
                    std::cout << "Error: Failed to decode text at 0x" << std::hex << std::uppercase << message_offset
                              << std::dec << ": " << text_error << std::endl;
                    continue;
                }

                const std::vector<uint8_t> message_bytes(
                    Rom.begin() + message_offset, Rom.begin() + message_offset + consumed_bytes);
                if (message_bytes.empty()) {
                    continue;
                }

                std::ostringstream message_name_stream;
                message_name_stream << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << message;
                const std::string message_name = message_name_stream.str();
                const std::filesystem::path relative_path =
                    std::filesystem::path("texts") / language_name / text_category_name(category) /
                    ("message_" + message_name + ".json");
                if (!PortAssetPipeline::WriteEditableText(config.outputRoot / relative_path, message_bytes, &text_error)) {
                    std::cout << "Error: Failed to write editable text " << relative_path << ": " << text_error
                              << std::endl;
                    continue;
                }

                nlohmann::json json_message;
                json_message["index"] = message;
                json_message["text_id"] = (category << 8) | message;
                json_message["rom_offset"] = message_offset;
                json_message["file"] = json_path_string(relative_path);
                json_message["size"] = message_bytes.size();
                json_message["preview"] = symbolic_text;
                json_messages.push_back(std::move(json_message));
            }

            json_category["messages"] = std::move(json_messages);
            json_categories[std::to_string(category)] = std::move(json_category);
        }

        json_language["categories"] = std::move(json_categories);
        json_root[std::to_string(language)] = std::move(json_language);
    }

    std::ofstream json_file(config.outputRoot / "texts.json");
    json_file << json_root.dump(4);
    std::cout << "Finished writing texts.json" << std::endl;
    return true;
}


inline bool extract_assets(const Config& config)
{
    const auto lookup = build_embedded_asset_lookup();
    std::vector<uint32_t> extra_boundaries = collect_area_table_data_offsets(config);
    const std::vector<uint32_t> sprite_animation_tables = collect_sprite_animation_table_offsets(config);
    extra_boundaries.insert(extra_boundaries.end(), sprite_animation_tables.begin(), sprite_animation_tables.end());
    const std::vector<uint32_t> boundaries = build_inference_boundaries(lookup, extra_boundaries);

    const std::vector<ExtractedPaletteRecord> extracted_palettes = extract_all_palettes(config);
    extract_all_palette_groups(config, extracted_palettes);
    extract_all_gfx(config.gfxGroupsTableOffset, config.gfxGroupsTableLength, config.globalGfxAndPalettesOffset,
                    config.language, config.outputRoot);
    extract_all_tilemaps(config);
    extract_area_room_headers(config);
    extract_area_map_table(config, config.areaTileSetsTableOffset, lookup, boundaries, "area_tile_sets.json",
                           "generated/mapdata", false);
    extract_area_map_table(config, config.areaRoomMapsTableOffset, lookup, boundaries, "area_room_maps.json",
                           "generated/mapdata", false);
    extract_area_map_table(config, config.areaTilesTableOffset, lookup, boundaries, "area_tiles.json",
                           "generated/mapdata", true);
    extract_area_tables(config, lookup, boundaries);
    extract_sprite_ptrs(config, lookup, boundaries);
    extract_texts(config);

    /* Final sweep: extract everything in the embedded asset index that the
     * specialized passes above didn't already produce. This covers data that's
     * referenced indirectly (e.g. data_080D5360/* room-property data pointed
     * at by gUnk_additional_* offsets reached from compiled code rather than
     * from area tables). Without this, release tarballs miss those subtrees
     * and rooms that depend on them silently misbehave (lily pads stationary,
     * doors not appearing, etc.). */
    const EmbeddedAssetEntry* idx = EmbeddedAssetIndex_Get();
    const u32 idx_count = EmbeddedAssetIndex_Count();
    uint32_t extracted_count = 0;
    for (u32 i = 0; i < idx_count; ++i) {
        const EmbeddedAssetEntry& entry = idx[i];
        const std::filesystem::path full_path = config.outputRoot / entry.path;
        if (std::filesystem::exists(full_path)) {
            continue; /* already extracted by a specialized pass */
        }
        const std::vector<uint8_t> data = extract_bytes(entry.offset, entry.size);
        if (write_binary_file(full_path, data)) {
            ++extracted_count;
        }
    }
    std::cout << "Final sweep: extracted " << extracted_count << " additional indexed assets" << std::endl;
    return true;
}
