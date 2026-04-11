#include <assets_extractor.hpp>

int main(int argc, char* argv[])
{
    if (!load_rom("baserom.gba")) {
        std::cerr << "Failed to load ROM." << std::endl;
        return 1;
    }
    //create folder assets if it doesn't exist
    std::filesystem::path assets_folder = "assets";
    if (!std::filesystem::exists(assets_folder)) {
        std::filesystem::create_directory(assets_folder);
    }
    Config config;
    config.gfxGroupsTableOffset = 0x100AA8;
    config.gfxGroupsTableLength = 133;
    extract_assets(config);

    return 0;
}