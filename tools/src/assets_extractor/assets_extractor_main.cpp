#include <assets_extractor.hpp>
#include "port_asset_pipeline.hpp"

extern "C" {
u8* gRomData = nullptr;
u32 gRomSize = 0;
}

static std::filesystem::path find_project_root(const std::filesystem::path& start_path)
{
    std::error_code ec;
    std::filesystem::path current = std::filesystem::absolute(start_path, ec);
    if (ec) {
        current = start_path;
    }

    if (!std::filesystem::is_directory(current, ec)) {
        current = current.parent_path();
    }

    while (!current.empty()) {
        if (std::filesystem::exists(current / "xmake.lua")) {
            return current;
        }

        const std::filesystem::path parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }

    return {};
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

    // Locate output base. Two layouts are supported:
    //  - Developer tree: first ancestor with xmake.lua → write under <root>/build/pc/.
    //  - Release tarball (no xmake.lua anywhere): write directly next to the
    //    binary, so tmc_pc (which looks for ./assets[_src] beside the exe) finds them.
    std::filesystem::path project_root = find_project_root(std::filesystem::current_path());
    if (project_root.empty()) {
        project_root = find_project_root(executable_dir);
    }

    std::filesystem::path editable_assets_folder;
    std::filesystem::path runtime_assets_folder;
    if (!project_root.empty()) {
        editable_assets_folder = project_root / "build/pc/assets_src";
        runtime_assets_folder  = project_root / "build/pc/assets";
    } else {
        editable_assets_folder = executable_dir / "assets_src";
        runtime_assets_folder  = executable_dir / "assets";
        std::cout << "No xmake.lua found; running in release mode, output beside the executable: "
                  << executable_dir << std::endl;
    }

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

    return 0;
}
