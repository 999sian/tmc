#include "assets_extractor_api.hpp"
#include "port_asset_log.hpp"
#include "global.h"

#include <fmt/format.h>

#include <filesystem>
#include <string>
#include <string_view>

/* Standalone binary owns these globals; in tmc_pc they're defined in
 * port_rom.c and the API simply mirrors Rom.data()/.size() into them.
 * port_gba_mem.h declares them with C++ linkage when compiled from a
 * .cpp TU, so match that here rather than re-introducing extern "C". */
u8* gRomData = nullptr;
u32 gRomSize = 0;

int main(int argc, char* argv[])
{
    bool verbose = false;
    bool runtime_only = false;
    bool force = false;
    bool pack_runtime = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        } else if (arg == "--runtime-only") {
            runtime_only = true;
        } else if (arg == "--force" || arg == "-f") {
            force = true;
        } else if (arg == "--pak") {
            pack_runtime = true;
        } else if (arg == "--help" || arg == "-h") {
            fmt::print("Usage: asset_extractor [--verbose] [--runtime-only] [--force] [--pak]\n"
                       "  --verbose       Print per-asset notes/warnings.\n"
                       "  --runtime-only  Skip writing the editable assets_src/ tree.\n"
                       "  --force         Re-extract even if assets/ are already up to date.\n"
                       "  --pak           Pack runtime assets into per-category .pak archives\n"
                       "                  instead of writing thousands of loose files.\n");
            return 0;
        }
    }

    std::filesystem::path executable_dir;
    if (argc > 0) {
        executable_dir = AssetExtractorApi::FindExecutableDirectory(argv[0]);
    }
    if (executable_dir.empty()) {
        executable_dir = std::filesystem::current_path();
    }

    AssetExtractorApi::Options opt;
    opt.rom_path = executable_dir / "baserom.gba";
    opt.editable_root = executable_dir / "assets_src";
    opt.runtime_root = executable_dir / "assets";
    opt.runtime_only = runtime_only;
    opt.pack_runtime = pack_runtime;
    opt.force = force;
    opt.verbose = verbose;

    std::string err;
    if (!AssetExtractorApi::ExtractAssets(opt, &err)) {
        PortAssetLog::Reporter::Instance().Error(err);
        return 1;
    }
    return 0;
}
