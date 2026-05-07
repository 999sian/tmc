#include "asset_extractor_runner.h"

#include <cstdint>
#include <filesystem>
#include <iostream>

#include <cstddef>
#include <fstream>

#include <cstddef>
#include <fstream>

extern "C" {
std::uint8_t* gRomData = nullptr;
std::uint32_t gRomSize = 0;

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

    std::string error;
    if (!RunEmbeddedAssetExtractor(executable_dir, &error)) {
        std::cerr << "Failed to extract assets: " << error << std::endl;
        return 1;
    }

    /* Always emit sounds.json next to the binary (= where tmc_pc launches
     * from). Same directory as assets_src/ and assets/. */
    write_sounds_json(executable_dir / "sounds.json");

    /* Always emit sounds.json next to the binary (= where tmc_pc launches
     * from). Same directory as assets_src/ and assets/. */
    write_sounds_json(executable_dir / "sounds.json");

    return 0;
}
