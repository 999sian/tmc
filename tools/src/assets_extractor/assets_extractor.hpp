#pragma once

#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <iostream>
#include <nlohmann/json.hpp>

struct Config
{
    uint32_t gfxGroupsTableOffset;
    uint32_t gfxGroupsTableLength;
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
    uint32_t compressed_index = 0;
    while (compressed_index < compressed_data.size()) {
        uint8_t flag = compressed_data[compressed_index++];
        for (int i = 0; i < 8 && compressed_index < compressed_data.size(); ++i) {
            if (flag & (1 << i)) {
                // Literal byte
                uncompressed_data.push_back(compressed_data[compressed_index++]);
            } else {
                // Back-reference
                if (compressed_index + 1 >= compressed_data.size()) break; // Ensure we have enough data for back-reference
                uint16_t back_ref = compressed_data[compressed_index++] | (compressed_data[compressed_index++] << 8);
                uint16_t offset = back_ref & 0x0FFF; // Lower 12 bits for offset
                uint16_t length = (back_ref >> 12) + 3; // Upper 4 bits for length, plus minimum length of 3

                uint32_t start_pos = uncompressed_data.size() - offset;
                for (uint32_t j = 0; j < length && start_pos + j < uncompressed_data.size(); ++j) {
                    uncompressed_data.push_back(uncompressed_data[start_pos + j]);
                }
            }
        }
    }
    return true;
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

inline bool bin_to_bmp(const std::vector<uint8_t>& gfx_data, const std::string& output_path, bool indexed, uint8_t bpp = 4)
{
    if (gfx_data.empty()) {
        return false;
    }

    // Calculate dimensions (assuming square aspect ratio for now)
    uint32_t pixels_per_byte = 8 / bpp;
    uint32_t total_pixels = (gfx_data.size() * pixels_per_byte);
    uint32_t width = static_cast<uint32_t>(std::sqrt(total_pixels));
    uint32_t height = (total_pixels + width - 1) / width; // Round up
    
    // Align width to 4-byte boundary for BMP format
    uint32_t row_size = ((width * bpp + 31) / 32) * 4;
    
    // BMP Header structure
    struct BMPHeader {
        uint16_t signature;      // "BM"
        uint32_t file_size;
        uint32_t reserved;       // 0
        uint32_t pixel_data_offset;
    } bmp_header;
    
    // DIB Header (BITMAPINFOHEADER)
    struct DIBHeader {
        uint32_t header_size;    // 40 bytes
        int32_t width;
        int32_t height;
        uint16_t planes;         // 1
        uint16_t bits_per_pixel;
        uint32_t compression;    // 0 (none)
        uint32_t image_size;
        int32_t x_pixels_per_meter;
        int32_t y_pixels_per_meter;
        uint32_t colors_used;
        uint32_t colors_important;
    } dib_header;
    
    // Create color palette (grayscale for indexed)
    std::vector<uint32_t> palette;
    if (indexed && bpp == 4) {
        // 16-color grayscale palette
        for (int i = 0; i < 16; ++i) {
            uint8_t gray = (i * 255) / 15;
            palette.push_back((gray << 16) | (gray << 8) | gray | 0xFF000000);
        }
    }
    
    // Setup headers
    bmp_header.signature = 0x4D42;  // "BM"
    bmp_header.reserved = 0;
    bmp_header.pixel_data_offset = 14 + 40 + (palette.size() * 4);
    bmp_header.file_size = bmp_header.pixel_data_offset + (row_size * height);
    
    dib_header.header_size = 40;
    dib_header.width = width;
    dib_header.height = height;
    dib_header.planes = 1;
    dib_header.bits_per_pixel = bpp;
    dib_header.compression = 0;
    dib_header.image_size = row_size * height;
    dib_header.x_pixels_per_meter = 2835;  // 72 DPI
    dib_header.y_pixels_per_meter = 2835;
    dib_header.colors_used = palette.size();
    dib_header.colors_important = 0;
    
    // Write BMP file
    std::ofstream bmp_file(output_path, std::ios::binary);
    if (!bmp_file) {
        return false;
    }
    
    // Write BMP header
    bmp_file.write(reinterpret_cast<const char*>(&bmp_header.signature), 2);
    bmp_file.write(reinterpret_cast<const char*>(&bmp_header.file_size), 4);
    bmp_file.write(reinterpret_cast<const char*>(&bmp_header.reserved), 4);
    bmp_file.write(reinterpret_cast<const char*>(&bmp_header.pixel_data_offset), 4);
    
    // Write DIB header
    bmp_file.write(reinterpret_cast<const char*>(&dib_header), 40);
    
    // Write color palette
    for (const auto& color : palette) {
        bmp_file.write(reinterpret_cast<const char*>(&color), 4);
    }
    
    // Write pixel data (bottom-up in BMP format)
    std::vector<uint8_t> row_data(row_size, 0);
    for (int32_t y = height - 1; y >= 0; --y) {
        std::fill(row_data.begin(), row_data.end(), 0);
        
        for (uint32_t x = 0; x < width; ++x) {
            uint32_t pixel_index = y * width + x;
            uint32_t byte_index = (pixel_index * bpp) / 8;
            uint32_t bit_offset = (pixel_index * bpp) % 8;
            
            if (byte_index < gfx_data.size()) {
                uint8_t pixel_value = (gfx_data[byte_index] >> bit_offset) & ((1 << bpp) - 1);
                uint32_t byte_pos = (x * bpp) / 8;
                uint32_t bit_pos = (x * bpp) % 8;
                
                if (byte_pos < row_data.size()) {
                    row_data[byte_pos] |= (pixel_value << bit_pos);
                }
            }
        }
        
        bmp_file.write(reinterpret_cast<const char*>(row_data.data()), row_size);
    }
    
    bmp_file.close();
    return true;
}

struct GfxMetadata
{
    uint16_t width;
    uint16_t height;
    uint8_t bpp;       // 1, 2, 4, or 8 bits per pixel
    bool is_indexed;   // true for palette-based, false for truecolor
};

// Hint detection: use file naming convention or size heuristics
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
    
    // Calculate total pixels
    uint32_t total_pixels = (size * 8) / meta.bpp;
    
    // Ensure minimum dimensions (at least 8x8 GBA tile)
    if (total_pixels == 0) {
        meta.width = 8;
        meta.height = 8;
        std::cout << "Warning: Empty/tiny gfx data at 0x" << std::hex << src << ", using 8x8" << std::dec << std::endl;
        return meta;
    }
    
    uint32_t sqrt_pixels = static_cast<uint32_t>(std::sqrt(total_pixels));
    
    meta.width = ((sqrt_pixels + 7) / 8) * 8;
    if (meta.width == 0) meta.width = 8; 
    
    meta.height = ((total_pixels + meta.width - 1) / meta.width);
    meta.height = ((meta.height + 7) / 8) * 8;
    if (meta.height == 0) meta.height = 8;  
    
    return meta;
}

bool extract_gfx(uint32_t src, uint32_t size, bool compressed, uint8_t bpp = 4, uint16_t width = 0, uint16_t height = 0)
{
    std::vector<uint8_t> gfx_data = extract_bytes(src, size);
    if (compressed) {
        std::vector<uint8_t> uncompressed_data;
        if (!lz77_uncompress(gfx_data, uncompressed_data)) {
            std::cout << "Error: Failed to uncompress gfx data at 0x" << std::hex << src << std::dec << std::endl;
            return false;
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
    std::filesystem::path gfx_folder = "assets/gfx";
    if (!std::filesystem::exists(gfx_folder)) {
        std::filesystem::create_directory(gfx_folder);
    }
    
    // Write binary file with metadata in filename
    std::stringstream ss;
    ss << std::hex << src;
    std::string hex_src = ss.str();
    std::filesystem::path bin_output = "assets/gfx/gfx_" + hex_src + "_" + std::to_string(meta.width) + "x" + 
                                       std::to_string(meta.height) + "_" + std::to_string(meta.bpp) + "bpp" +
                                       (compressed ? "_compressed" : "_uncompressed") + ".bin";
    
    std::ofstream output_file(bin_output, std::ios::binary);
    if (!output_file) {
        std::cout << "Error: Failed to create output file for gfx data at 0x" << std::hex << src << std::dec << std::endl;
        return false;
    }
    output_file.write(reinterpret_cast<const char*>(gfx_data.data()), gfx_data.size());
    std::cout << "Extracted gfx: " << meta.width << "x" << meta.height << " @ " << (int)meta.bpp << "bpp to " << bin_output << std::endl;
    
    // Try to export as BMP for visualization
    std::filesystem::path bmp_output = bin_output;
    bmp_output.replace_extension(".bmp");
    if (!bin_to_bmp(gfx_data, bmp_output.string(), meta.is_indexed, meta.bpp)) {
        std::cout << "Note: Could not generate BMP preview" << std::endl;
    }
    
    return true;
}



inline bool extract_all_gfx(uint32_t gfx_group_address , uint32_t gfx_group_table_length = 133)
{
    std::vector<uint32_t> gfx_group_addresses = extract_gfx_group_addresses(gfx_group_address, gfx_group_table_length);
        std::cout << "Found " << gfx_group_addresses.size() << " gfx groups." << std::endl;
    std::vector<GfxGroup> gfx_groups;
    for (uint32_t address : gfx_group_addresses) {
        GfxGroup gfx_group = extract_gfx_group(address);
        gfx_groups.push_back(gfx_group);
    }
    //write gfx groups to json file
    std::cout << "Writing gfx groups to JSON file..." << std::endl;
    nlohmann::json json_gfx_groups;
    for (size_t i = 0; i < gfx_groups.size(); ++i) {
        const GfxGroup& group = gfx_groups[i];
        nlohmann::json json_group;
        for (const auto& element : group) {
            nlohmann::json json_element;
            json_element["src"] = element.src;
            json_element["unknown"] = element.unknown;
            json_element["dest"] = element.dest;
            json_element["size"] = element.size;
            json_element["compressed"] = element.compressed;
            json_element["terminator"] = element.terminator;
            json_group.push_back(json_element);
        }
        json_gfx_groups[std::to_string(i+1)] = json_group;
    }
    std::ofstream json_file("assets/gfx_groups.json");
    json_file << json_gfx_groups.dump(4);
    std::cout << "Finished writing gfx groups to JSON file." << std::endl;
    for (const GfxGroup& group : gfx_groups) {
        for (const GfxGroupElement& element : group) {
            if (!extract_gfx(element.src, element.size, element.compressed)) {
                std::cout << "Error: Failed to extract gfx data for element with src 0x" << std::hex << element.src << std::dec << std::endl;
            }
        }
    }
    return true;
}


inline bool extract_assets(const Config& config)
{
    extract_all_gfx(config.gfxGroupsTableOffset, config.gfxGroupsTableLength);
    return true;
}