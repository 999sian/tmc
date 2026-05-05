#include "assets_extractor_api.hpp"

#include <assets_extractor.hpp>
#include "port_asset_pipeline.hpp"
#include "port_asset_log.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace AssetExtractorApi {

namespace {

struct RomFingerprint {
    uint64_t size = 0;
    int64_t mtime = 0;
};

RomFingerprint ComputeRomFingerprint(const std::filesystem::path& rom_path)
{
    RomFingerprint fp;
    std::error_code ec;
    fp.size = std::filesystem::file_size(rom_path, ec);
    if (ec) {
        fp.size = 0;
    }
    auto t = std::filesystem::last_write_time(rom_path, ec);
    if (!ec) {
        fp.mtime = static_cast<int64_t>(t.time_since_epoch().count());
    }
    return fp;
}

bool RuntimeAssetsUpToDateImpl(const std::filesystem::path& runtime_root,
                               const RomFingerprint& fp,
                               bool pack_runtime)
{
    const std::filesystem::path state_path = runtime_root / ".asset_build_state.json";
    if (!std::filesystem::exists(state_path)) {
        return false;
    }
    std::ifstream in(state_path);
    if (!in) {
        return false;
    }
    try {
        nlohmann::json state;
        in >> state;
        if (!state.contains("rom_size") || !state.contains("rom_mtime")) {
            return false;
        }
        if (state["rom_size"].get<uint64_t>() != fp.size) {
            return false;
        }
        if (state["rom_mtime"].get<int64_t>() != fp.mtime) {
            return false;
        }
        const std::string desired = pack_runtime ? "v1" : "loose";
        const std::string recorded = state.value("pack_format", std::string("loose"));
        if (desired != recorded) {
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

void StampRomFingerprint(const std::filesystem::path& runtime_root,
                         const RomFingerprint& fp,
                         bool pack_runtime)
{
    const std::filesystem::path state_path = runtime_root / ".asset_build_state.json";
    nlohmann::json state;
    if (std::filesystem::exists(state_path)) {
        std::ifstream in(state_path);
        if (in) {
            try { in >> state; } catch (...) { state = nlohmann::json::object(); }
        }
    }
    if (!state.is_object()) {
        state = nlohmann::json::object();
    }
    state["rom_size"] = fp.size;
    state["rom_mtime"] = fp.mtime;
    state["pack_format"] = pack_runtime ? "v1" : "loose";

    PortAssetLog::EnsureDir(runtime_root);
    std::ofstream out(state_path);
    if (out) {
        out << state.dump();
    }
}

/* Wipe stale outputs so a mode switch (loose <-> pak) doesn't leave
 * orphan files in runtime_root. baserom.gba and the build-state file
 * are preserved so callers don't accidentally nuke the user's ROM. */
void WipeStaleRuntime(const std::filesystem::path& runtime_root, bool pack_runtime)
{
    std::error_code ec;
    if (pack_runtime) {
        for (const auto& entry : std::filesystem::directory_iterator(runtime_root, ec)) {
            const auto& name = entry.path().filename().string();
            if (name == ".asset_build_state.json" || name == "baserom.gba") {
                continue;
            }
            if (entry.is_directory(ec)) {
                std::filesystem::remove_all(entry.path(), ec);
            } else if (entry.path().extension() == ".pak") {
                std::filesystem::remove(entry.path(), ec);
            }
        }
    } else {
        for (const auto& entry : std::filesystem::directory_iterator(runtime_root, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            if (entry.path().extension() == ".pak") {
                std::filesystem::remove(entry.path(), ec);
            }
        }
    }
}

}  // namespace

std::filesystem::path FindExecutableDirectory(const std::filesystem::path& argv0)
{
    /* Prefer the OS-provided "where am I" mechanism over argv[0]: under some
     * launchers (most notably AppImage-wrapped IDE terminals) argv[0] and
     * the inherited cwd both point at the launcher's working directory
     * rather than the directory containing the binary, which makes a naive
     * absolute(argv[0]).parent_path() write the assets tree miles away
     * from the binary. /proc/self/exe (Linux) and GetModuleFileName
     * (Windows) always resolve to the actual binary on disk, regardless
     * of how it was invoked. */
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        std::filesystem::path self(buf);
        return self.parent_path();
    }
#else
    std::error_code ec;
    std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec && !self.empty()) {
        return self.parent_path();
    }
#endif

    if (argv0.empty()) {
        return {};
    }

    std::error_code ec2;
    std::filesystem::path absolute_path = std::filesystem::absolute(argv0, ec2);
    if (ec2) {
        absolute_path = argv0;
    }

    if (std::filesystem::is_directory(absolute_path, ec2)) {
        return absolute_path;
    }

    return absolute_path.parent_path();
}

bool RuntimeUpToDate(const std::filesystem::path& runtime_root,
                     const std::filesystem::path& rom_path,
                     bool pack_runtime)
{
    const RomFingerprint fp = ComputeRomFingerprint(rom_path);
    if (fp.size == 0) {
        return false;
    }
    return RuntimeAssetsUpToDateImpl(runtime_root, fp, pack_runtime);
}

bool ExtractAssets(const Options& opt, std::string* error)
{
    auto fail = [&](std::string msg) {
        if (error) *error = std::move(msg);
        return false;
    };

    if (opt.editable_root.empty() || opt.runtime_root.empty()) {
        return fail("editable_root and runtime_root must be set");
    }

    auto& reporter = PortAssetLog::Reporter::Instance();
    reporter.SetVerbose(opt.verbose);

    /* Compute fingerprint from rom_path when available; for the
     * in-memory path we only have the buffer, so the fingerprint is
     * synthesized from size + 0 mtime. The engine path always has
     * rom_path (it's the same baserom.gba it just loaded), so this
     * mostly matters for unusual standalone-API callers. */
    RomFingerprint rom_fp;
    if (!opt.rom_path.empty()) {
        rom_fp = ComputeRomFingerprint(opt.rom_path);
    } else {
        rom_fp.size = opt.rom_buffer.size();
        rom_fp.mtime = 0;
    }

    if (!opt.force && rom_fp.size > 0 &&
        RuntimeAssetsUpToDateImpl(opt.runtime_root, rom_fp, opt.pack_runtime)) {
        reporter.Finish("assets are up to date");
        return true;
    }

    if (!opt.rom_buffer.empty()) {
        if (!load_rom_from_buffer(opt.rom_buffer)) {
            return fail("failed to copy ROM buffer");
        }
    } else {
        if (opt.rom_path.empty()) {
            return fail("either rom_path or rom_buffer must be provided");
        }
        if (!load_rom(opt.rom_path)) {
            return fail(fmt::format("failed to load ROM from {}", opt.rom_path.string()));
        }
    }
    /* Mirror to the C globals so legacy code paths inside the
     * extractor (and the engine, when this runs in-process) see the
     * same ROM bytes. The standalone binary defines these in main;
     * tmc_pc has its own definition in port_rom.c. Both port_gba_mem.h
     * declarations were already pulled in by assets_extractor.hpp. */
    gRomData = Rom.data();
    gRomSize = static_cast<u32>(Rom.size());

    if (!std::filesystem::exists(opt.editable_root)) {
        std::filesystem::create_directories(opt.editable_root);
    }
    if (!std::filesystem::exists(opt.runtime_root)) {
        std::filesystem::create_directories(opt.runtime_root);
    }

    const auto t0 = std::chrono::steady_clock::now();

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
    config.outputRoot = opt.editable_root;
    config.runtimeOutputRoot = opt.runtime_root;

    WipeStaleRuntime(opt.runtime_root, opt.pack_runtime);

    PakBuilderArray pak_builders;
    if (opt.pack_runtime) {
        config.packRuntime = true;
        config.pakBuilders = &pak_builders;
    }

    extract_assets(config);

    /* Drain BackgroundWriter so all aggregate JSON writes are flushed
     * before we record the build state or hand off to BuildRuntimeAssets. */
    try {
        PortAssetLog::BackgroundWriter::Instance().Wait();
    } catch (const std::exception& e) {
        return fail(fmt::format("background JSON writer failed: {}", e.what()));
    }

    if (opt.pack_runtime) {
        const auto& names = pak_category_names();
        std::vector<std::string> pak_errors(kPakCategoryCount);
        reporter.BeginPhase("paks", kPakCategoryCount);
        PortAssetLog::ParallelFor<std::size_t>(0, kPakCategoryCount, [&](std::size_t i) {
            if (pak_builders[i].Size() == 0) {
                reporter.Tick();
                return;
            }
            const std::filesystem::path out = opt.runtime_root / names[i];
            std::string err;
            if (!pak_builders[i].Write(out, &err)) {
                pak_errors[i] = std::move(err);
            }
            reporter.Tick();
        });
        reporter.EndPhase();
        for (std::size_t i = 0; i < kPakCategoryCount; ++i) {
            if (!pak_errors[i].empty()) {
                return fail(fmt::format("pak write failed ({}): {}", names[i], pak_errors[i]));
            }
        }
        if (opt.phase_done) opt.phase_done("paks");
    }

    std::string build_error;
    if (config.runtimeOutputRoot.empty()) {
        if (!PortAssetPipeline::BuildRuntimeAssets(opt.editable_root, opt.runtime_root, &build_error)) {
            return fail(fmt::format("failed to build runtime assets: {}", build_error));
        }
    } else if (!PortAssetPipeline::WriteBuildStateFile(opt.editable_root, opt.runtime_root, &build_error)) {
        return fail(fmt::format("failed to write build state: {}", build_error));
    }

    StampRomFingerprint(opt.runtime_root, rom_fp, opt.pack_runtime);

    if (opt.runtime_only) {
        std::error_code ec;
        std::filesystem::remove_all(opt.editable_root, ec);
    }

    const auto t1 = std::chrono::steady_clock::now();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    reporter.Finish(fmt::format("asset extraction complete in {}ms", elapsed_ms));

    return true;
}

}  // namespace AssetExtractorApi
