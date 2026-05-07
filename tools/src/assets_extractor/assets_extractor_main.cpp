#include "asset_extractor_runner.h"

#include <cstdint>
#include <filesystem>
#include <iostream>

extern "C" {
std::uint8_t* gRomData = nullptr;
std::uint32_t gRomSize = 0;
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

    return 0;
}
