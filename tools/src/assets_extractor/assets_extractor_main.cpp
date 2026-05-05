#include <assets_extractor.hpp>
#include "port_asset_pipeline.hpp"

#include <cstddef>
#include <fstream>

extern "C" {
u8* gRomData = nullptr;
u32 gRomSize = 0;

/* Defined in embedded_sounds_json.cpp — assets/sounds.json baked in via
 * xmake's utils.bin2c rule. See that file for the rationale. */
extern const unsigned char kEmbeddedSoundsJson[];
extern const std::size_t kEmbeddedSoundsJsonSize;
}

/* Write the embedded copy of sounds.json next to the asset_extractor binary
 * (= same directory tmc_pc launches from). This guarantees the runtime
 * sound loader finds it even if the release tarball was built from a tree
 * that forgot to copy assets/sounds.json into the artifact (#50: v0.1.6
 * Linux/Windows tarballs shipped without sounds.json). Re-running the
 * extractor on a broken tarball is now sufficient to restore audio. */
static void write_sounds_json(const std::filesystem::path& output_path)
{
    std::error_code ec;
    if (std::filesystem::exists(output_path, ec) && !ec) {
        const auto existing_size = std::filesystem::file_size(output_path, ec);
        if (!ec && existing_size == kEmbeddedSoundsJsonSize) {
            return; /* already correct, leave user-modified files alone */
        }
    }

    std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << "Warning: could not write " << output_path
                  << " (sounds.json) — runtime audio will be silent unless "
                     "the file is provided manually." << std::endl;
        return;
    }
    out.write(reinterpret_cast<const char*>(kEmbeddedSoundsJson),
              static_cast<std::streamsize>(kEmbeddedSoundsJsonSize));
    if (!out) {
        std::cerr << "Warning: short write while emitting " << output_path
                  << std::endl;
        return;
    }
    std::cout << "Wrote sounds.json (" << kEmbeddedSoundsJsonSize
              << " bytes) to " << output_path << std::endl;
}

static std::filesystem::path find_executable_directory(const std::filesystem::path& executable_path)
{
    if (executable_path.empty()) {
        return {};
    }

    std::error_code ec;
    std::filesystem::path absolute_path = std::filesystem::absolute(executable_path, ec);
    if (ec) {
        absolute_path = executable_path;
    }

    if (std::filesystem::is_directory(absolute_path, ec)) {
        return absolute_path;
    }

    return absolute_path.parent_path();
}

int main(int argc, char* argv[])
{
    std::filesystem::path executable_dir;
    if (argc > 0) {
        executable_dir = find_executable_directory(argv[0]);
    }
    if (executable_dir.empty()) {
        executable_dir = std::filesystem::current_path();
    }

    // Always write next to the binary. In dev mode the binary lives at
    // <repo>/build/pc/asset_extractor, so this lands under build/pc/ as
    // before. In a release tarball, this lands beside the unpacked binary.
    // tmc_pc looks for ./assets[_src] next to its own exe (same directory
    // as asset_extractor in both flows), so a single rule covers both.
    const std::filesystem::path editable_assets_folder = executable_dir / "assets_src";
    const std::filesystem::path runtime_assets_folder  = executable_dir / "assets";

    const std::filesystem::path rom_path = executable_dir / "baserom.gba";
    if (!load_rom(rom_path)) {
        std::cerr << "Failed to load ROM from " << rom_path << std::endl;
        return 1;
    }
    gRomData = Rom.data();
    gRomSize = static_cast<u32>(Rom.size());

    if (!std::filesystem::exists(editable_assets_folder)) {
        std::filesystem::create_directories(editable_assets_folder);
    }

    Config config;
    config.gfxGroupsTableOffset = 0x100AA8;
    config.gfxGroupsTableLength = 133;
    config.paletteGroupsTableOffset = 0x0FF850;
    config.paletteGroupsTableLength = 208;
    config.globalGfxAndPalettesOffset = 0x5A2E80;
    config.mapDataOffset = 0x324AE4;
    config.areaRoomHeadersTableOffset = 0x11E214;
    config.areaTileSetsTableOffset = 0x10246C;
    config.areaRoomMapsTableOffset = 0x107988;
    config.areaTableTableOffset = 0x0D50FC;
    config.areaTilesTableOffset = 0x10309C;
    config.spritePtrsTableOffset = 0x0029B4;
    config.spritePtrsCount = 329;
    config.translationsTableOffset = 0x109214;
    config.language = 1;
    config.variant = "USA";
    config.outputRoot = editable_assets_folder;
    extract_assets(config);

    std::string build_error;
    if (!PortAssetPipeline::BuildRuntimeAssets(editable_assets_folder, runtime_assets_folder, &build_error)) {
        std::cerr << "Failed to build runtime assets: " << build_error << std::endl;
        return 1;
    }

    /* Always emit sounds.json next to the binary (= where tmc_pc launches
     * from). Same directory as assets_src/ and assets/. */
    write_sounds_json(executable_dir / "sounds.json");

    return 0;
}
