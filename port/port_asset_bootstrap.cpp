/* Order matters: std headers first so the GBA-style `min`/`max`
 * macros from include/global.h (pulled in transitively via
 * port_asset_bootstrap.h) don't collide with std::min/std::max
 * inside <algorithm>. */
#include <algorithm>
#include "port_asset_bootstrap.h"
#include "port_asset_pipeline.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <future>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <SDL3/SDL.h>

#include "port_asset_bootstrap.h"
/* global.h (transitively included by port_asset_bootstrap.h) defines
 * GBA-style min/max as object-style macros, which break C++ headers
 * that overload std::min/std::max. Strip them here so the rest of
 * this TU can use the standard library cleanly. */
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include "port_asset_loader.h"
#include "port_asset_log.hpp"
#include "port_asset_pipeline.hpp"

#include "assets_extractor_api.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <climits>
#include <cstdlib>
#include <mach-o/dyld.h>
#else
#include <climits>
#include <unistd.h>
#endif

int Port_LooseAssetsRequested = 0;

namespace {

/* Approximate number of distinct phases the extractor walks through.
 * Used to show "(N / EXPECTED)" beside the bar so the user has a
 * sense of progress between phase boundaries. The actual phase
 * count varies slightly by ROM contents but is stable enough for
 * UI purposes; we clamp the displayed numerator at the denominator
 * if it overshoots. */
constexpr int kExpectedPhaseCount = 9;

/* Lock-free snapshot of extractor progress. Producer is the
 * Reporter callback (called from worker threads); consumer is the
 * GUI thread that polls every ~50 ms while the bar is on screen. */
struct ProgressSnapshot {
    std::atomic<std::size_t> done{0};
    std::atomic<std::size_t> total{0};
    std::atomic<int> phase_index{0};
    std::atomic<bool> running{true};

    /* Phase name lives behind a mutex because std::string isn't
     * trivially atomic. The GUI takes a copy under lock once per
     * frame which is fine at 60 Hz. */
    std::mutex name_mu;
    std::string phase_name;
    std::string last_phase_name;  // used to detect phase transitions
};

void MountPaksForRoot(const std::filesystem::path& root) {
    if (Port_LooseAssetsRequested) {
        std::fprintf(stderr, "[ASSET] paks present but disabled by --loose-assets\n");
        return;
    }
    const std::filesystem::path assetsDir = root / "assets";
    const int mounted = Port_MountAssetPaks(assetsDir.string().c_str());
    if (mounted > 0) {
        std::fprintf(stderr, "[ASSET] paks mounted: %d (%d entries)\n", mounted,
                     Port_PakEntryCount());
    } else {
        std::fprintf(stderr, "[ASSET] no paks mounted; using loose files\n");
    }
}

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
    /* macOS has no /proc filesystem; readlink("/proc/self/exe") fails.
     * _NSGetExecutablePath returns the launch path which may include
     * symlinks/relative segments, so realpath() it for a canonical form. */
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
    return std::nullopt;
#else
    char buffer[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer));
    if (len > 0 && static_cast<size_t>(len) < sizeof(buffer)) {
        return std::filesystem::path(std::string(buffer, static_cast<size_t>(len))).parent_path();
    }
    return std::nullopt;
#endif
}

std::filesystem::path PreferredAssetRoot() {
    const auto exeDir = GetExecutableDirectory();
    if (exeDir.has_value()) {
        return *exeDir;
    }
    std::error_code ec;
    const auto cwd = std::filesystem::current_path(ec);
    return ec ? std::filesystem::path(".") : cwd;
}

bool EnsureSoundsMetadata(const std::filesystem::path& root, std::string& error) {
    if (std::filesystem::exists(root / "sounds.json") || std::filesystem::exists(root / "assets" / "sounds.json")) {
        return true;
    }

    for (std::filesystem::path probe = root; !probe.empty(); probe = probe.parent_path()) {
        const std::filesystem::path source = probe / "assets" / "sounds.json";
        if (std::filesystem::exists(source)) {
            std::error_code ec;
            std::filesystem::copy_file(
                source, root / "sounds.json", std::filesystem::copy_options::overwrite_existing, ec
            );
            if (ec) {
                error = "failed to copy sounds.json: " + ec.message();
                return false;
            }
            return true;
        }

        const std::filesystem::path parent = probe.parent_path();
        if (parent == probe) {
            break;
        }
    }

    error = "sounds.json was not found";
    return false;
}

/* ----------------------------------------------------------------
 *  5x7 bitmap font for the progress bar UI.
 *
 *  Extended from the original A/C/E/G/I/N/R/S/T/X/. set so we can
 *  spell out arbitrary phase names ("loading palettes", "writing
 *  paks", etc.) plus digits for the "3 / 9" counter.
 * ---------------------------------------------------------------- */
using GlyphRows = std::array<unsigned char, 7>;

GlyphRows GlyphFor(char c) {
    switch (c) {
        case 'A': return { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
        case 'B': return { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E };
        case 'C': return { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E };
        case 'D': return { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E };
        case 'E': return { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F };
        case 'F': return { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 };
        case 'G': return { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F };
        case 'H': return { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
        case 'I': return { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F };
        case 'J': return { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C };
        case 'K': return { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 };
        case 'L': return { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F };
        case 'M': return { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 };
        case 'N': return { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 };
        case 'O': return { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
        case 'P': return { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 };
        case 'Q': return { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D };
        case 'R': return { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 };
        case 'S': return { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E };
        case 'T': return { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };
        case 'U': return { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
        case 'V': return { 0x11, 0x11, 0x11, 0x11, 0x0A, 0x0A, 0x04 };
        case 'W': return { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A };
        case 'X': return { 0x11, 0x0A, 0x04, 0x04, 0x04, 0x0A, 0x11 };
        case 'Y': return { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 };
        case 'Z': return { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F };
        case '0': return { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E };
        case '1': return { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E };
        case '2': return { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F };
        case '3': return { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E };
        case '4': return { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 };
        case '5': return { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E };
        case '6': return { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E };
        case '7': return { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 };
        case '8': return { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E };
        case '9': return { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C };
        case '/': return { 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10 };
        case '-': return { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 };
        case '_': return { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F };
        case '%': return { 0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03 };
        case '.': return { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C };
        case ':': return { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 };
        default:  return { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    }
}

void DrawText(SDL_Renderer* renderer, std::string_view text, float x, float y, float scale,
              SDL_Color color = {235, 240, 245, 255}) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    /* Uppercase the text up front so phase names submitted as "palettes"
     * still render via the uppercase-only glyph table. Cheaper and
     * smaller than maintaining a parallel lowercase set. */
    for (char raw : text) {
        char c = raw;
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - ('a' - 'A'));
        const GlyphRows glyph = GlyphFor(c);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if ((glyph[row] & (1 << (4 - col))) == 0) continue;
                SDL_FRect pixel = { x + col * scale, y + row * scale, scale, scale };
                SDL_RenderFillRect(renderer, &pixel);
            }
        }
        x += (c == ' ') ? scale * 4.0f : scale * 6.0f;
    }
}

float MeasureText(std::string_view text, float scale) {
    /* Mirrors the per-character advance in DrawText so callers can
     * compute centred positions without rendering twice. */
    float w = 0.0f;
    for (char c : text) {
        w += (c == ' ') ? scale * 4.0f : scale * 6.0f;
    }
    return w;
}

void DrawProgressScreen(SDL_Window* window, SDL_Renderer* renderer,
                        const ProgressSnapshot& snap) {
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);

    SDL_SetRenderDrawColor(renderer, 12, 16, 22, 255);
    SDL_RenderClear(renderer);

    const float fw = static_cast<float>(width);
    const float fh = static_cast<float>(height);
    const float scale = std::max(2.0f, std::round(fw / 240.0f));

    /* Header line, drawn once and unaffected by the ticking phase. */
    constexpr std::string_view kHeader = "EXTRACTING ASSETS";
    const float headerW = MeasureText(kHeader, scale);
    DrawText(renderer, kHeader, (fw - headerW) * 0.5f, fh * 0.30f, scale);

    /* Phase label (snapshot under lock since std::string isn't atomic). */
    std::string phaseName;
    {
        std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(snap.name_mu));
        phaseName = snap.phase_name.empty() ? std::string("preparing") : snap.phase_name;
    }
    const std::string phaseLine = std::string("loading ") + phaseName;
    const float phaseScale = std::max(1.5f, scale * 0.7f);
    const float phaseW = MeasureText(phaseLine, phaseScale);
    DrawText(renderer, phaseLine, (fw - phaseW) * 0.5f, fh * 0.46f, phaseScale,
             {180, 200, 220, 255});

    /* Progress bar geometry. */
    const float barWidth = fw * 0.65f;
    const float barHeight = std::max(8.0f * scale, fh * 0.04f);
    const float barX = (fw - barWidth) * 0.5f;
    const float barY = fh * 0.55f;

    const std::size_t done = snap.done.load(std::memory_order_acquire);
    const std::size_t total = snap.total.load(std::memory_order_acquire);
    const int phaseIdx = snap.phase_index.load(std::memory_order_acquire);
    const int phaseDen = std::max(kExpectedPhaseCount, phaseIdx + 1);

    const double inPhase = (total == 0) ? 1.0
                                        : static_cast<double>(done) / static_cast<double>(total);
    /* Stage-aware fraction: each completed phase is worth 1/phaseDen
     * of the bar, the active phase contributes its own internal
     * percentage. Clamped because the last phase often completes
     * with done > total when EndPhase fires before the GUI re-snaps. */
    const double overall =
        std::clamp((static_cast<double>(phaseIdx) + std::clamp(inPhase, 0.0, 1.0)) /
                       static_cast<double>(phaseDen),
                   0.0, 1.0);

    /* Bar border (light) + fill (warm accent). */
    SDL_SetRenderDrawColor(renderer, 60, 70, 80, 255);
    SDL_FRect border = { barX - 2.0f, barY - 2.0f, barWidth + 4.0f, barHeight + 4.0f };
    SDL_RenderFillRect(renderer, &border);
    SDL_SetRenderDrawColor(renderer, 18, 24, 32, 255);
    SDL_FRect inner = { barX, barY, barWidth, barHeight };
    SDL_RenderFillRect(renderer, &inner);
    SDL_SetRenderDrawColor(renderer, 86, 168, 124, 255);
    SDL_FRect fill = { barX, barY, static_cast<float>(barWidth * overall), barHeight };
    SDL_RenderFillRect(renderer, &fill);

    /* Footer: "PHASE n/m   pp%" */
    char footer[64];
    std::snprintf(footer, sizeof(footer), "%d/%d  %d%%",
                  std::min(phaseIdx + 1, phaseDen), phaseDen,
                  static_cast<int>(overall * 100.0 + 0.5));
    const float footerScale = std::max(1.5f, scale * 0.6f);
    const float footerW = MeasureText(footer, footerScale);
    DrawText(renderer, footer, (fw - footerW) * 0.5f,
             barY + barHeight + 1.5f * scale, footerScale,
             {160, 175, 195, 255});

    SDL_RenderPresent(renderer);
}

template <typename Task>
bool RunWithProgressScreen(SDL_Window* window, ProgressSnapshot& snap, Task task) {
    /* Adopt the renderer that SDL_CreateWindowAndRenderer made at
     * launch time. Calling SDL_CreateRenderer here would fail
     * because SDL3 enforces one renderer per window, and we'd
     * silently fall through to task() with no progress UI — which
     * is exactly how the EXTRACTING ASSETS bar disappeared after
     * the atomic-create switch. Fall back to creating one only if
     * for some reason the window doesn't have a renderer yet. */
    SDL_Renderer* renderer = SDL_GetRenderer(window);
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, nullptr);
    }
    if (!renderer) {
        return task();
    }

    auto future = std::async(std::launch::async, std::forward<Task>(task));
    while (future.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                /* Keep extracting so the install isn't left half-written. */
            }
        }
        DrawProgressScreen(window, renderer, snap);
    }

    /* One final paint so the bar visibly reaches 100% before we
     * hand the renderer off to Port_PPU_Init. We deliberately do
     * NOT destroy the renderer here — destroying it and letting
     * Port_PPU_Init create a new one for the same window causes a
     * visible compositor flash that reads as "extractor window
     * closed, game window opened". Port_PPU_Init now adopts this
     * renderer via SDL_GetRenderer(window), and PPU shutdown owns
     * its lifetime from then on. */
    DrawProgressScreen(window, renderer, snap);
    const bool ok = future.get();
    return ok;
}

#ifdef TMC_OVERLAP_EXTRACT_INIT
/* Phase name -> phase-gate categories. Mirrors the producer-side
 * BeginPhase names emitted by the extractor; anything unmapped is
 * treated as Misc (gate 8). 'paks' opens everything because the
 * writer pass covers all categories. Called only at phase
 * transitions, so the cost of the if/else cascade is negligible. */
void OpenGatesForCompletedPhase(std::string_view phase) {
    const auto open = [](int idx) { Port_AssetLoader_OpenGate(idx); };
    if (phase == "palettes" || phase == "palette_groups") open(1);
    else if (phase == "gfx") open(0);
    else if (phase == "tilemaps") open(4);
    else if (phase == "areas") { open(5); open(6); open(7); }
    else if (phase == "sprites") { open(3); open(2); }
    else if (phase == "texts") open(8);
    else if (phase == "sweep" || phase == "paks") Port_AssetLoader_OpenAllGates();
}
#endif

void InstallReporterCallback(ProgressSnapshot& snap) {
    /* Snapshot writer. Runs on the extractor's worker threads under
     * the Reporter mutex, so it must be O(few stores) and never
     * block. The only allocation is the one-shot string copy when
     * the phase name changes, which happens ~9 times per run. */
    PortAssetLog::Reporter::Instance().SetProgressCallback(
        [&snap](std::string_view phase, std::size_t done, std::size_t total) {
            snap.done.store(done, std::memory_order_release);
            snap.total.store(total, std::memory_order_release);

            std::lock_guard<std::mutex> lk(snap.name_mu);
            const std::string incoming(phase);
            if (incoming != snap.last_phase_name) {
                if (!snap.last_phase_name.empty()) {
                    snap.phase_index.fetch_add(1, std::memory_order_acq_rel);
#ifdef TMC_OVERLAP_EXTRACT_INIT
                    /* Phase boundary observed: the previous phase
                     * is finished, so re-open its gate(s) before we
                     * even hit the next BeginPhase event. */
                    OpenGatesForCompletedPhase(snap.last_phase_name);
#endif
                }
                snap.last_phase_name = incoming;
                snap.phase_name = incoming;
            }
        });
}

void ClearReporterCallback() {
    PortAssetLog::Reporter::Instance().SetProgressCallback({});
}

}  // namespace

extern "C" void Port_EnsureAssetsReadyWithDisplay(SDL_Window* window,
                                                  const u8* rom_data,
                                                  u32 rom_size) {
    const std::filesystem::path root = PreferredAssetRoot();
    const std::filesystem::path rom = root / "baserom.gba";
    const bool packMode = !Port_LooseAssetsRequested;

    /* Step 1: warm-launch fast path. Same ROM fingerprint + pack
     * mode as the recorded build → assets/ is current and we can
     * skip the renderer entirely. */
    if (AssetExtractorApi::RuntimeUpToDate(root / "assets", rom, packMode)) {
        MountPaksForRoot(root);
        return;
    }

    /* Step 2: spawn the extractor with a real progress bar. */
    ProgressSnapshot snap;
    InstallReporterCallback(snap);

#ifdef TMC_OVERLAP_EXTRACT_INIT
    /* Phase 7: shut all phase gates before extraction starts so the
     * engine threads block in LoadBinaryFileCached until the
     * relevant category is on disk. The phase_done callback below
     * re-opens them per-category as the extractor finishes. */
    Port_AssetLoader_BeginGated();
#endif

    AssetExtractorApi::Options opt;
    opt.rom_path = rom;
    if (rom_data != nullptr && rom_size > 0) {
        /* Phase 6: reuse the engine's already-loaded ROM buffer so
         * we don't pay for a second 16 MB read off disk. The
         * extractor copies bytes into its own vector internally. */
        opt.rom_buffer = std::span<const uint8_t>(rom_data, rom_size);
    }
    opt.editable_root = root / "assets_src";
    opt.runtime_root = root / "assets";
    opt.runtime_only = true;     // engine doesn't need the editable JSON tree
    opt.pack_runtime = packMode;
    opt.force = false;
    opt.verbose = false;


    std::string err;
    const bool ok = RunWithProgressScreen(window, snap, [&] {
        return AssetExtractorApi::ExtractAssets(opt, &err);
    });

    ClearReporterCallback();

#ifdef TMC_OVERLAP_EXTRACT_INIT
    /* Failsafe: if extraction errored partway through, open every
     * gate so engine threads waiting on LoadBinaryFileCached unblock
     * (and propagate the failure via the message box below) instead
     * of deadlocking. Idempotent if everything already succeeded. */
    Port_AssetLoader_OpenAllGates();
#endif

    if (ok) {
        MountPaksForRoot(root);
        /* Port_LoadRom (which already ran at this point) probed the
         * asset loader before assets/ existed. Re-trigger the scan
         * now that gfx_groups.json / palette_groups.json / area JSONs
         * are on disk so the engine sees them on the next lookup. */
        Port_AssetLoader_Reload();
        Port_LoadTextsFromAssets();
        Port_LoadSpritePtrsFromAssets();
        Port_LoadAreaTablesFromAssets();
        return;
    }

    const std::string message = err.empty() ? std::string("Asset extraction failed.") : err;
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Asset extraction failed", message.c_str(),
                             window);
}

extern "C" void Port_PaintBootSplash(SDL_Window* window, const char* message) {
    if (!window) return;
    /* Reuse whichever renderer is already attached to the window
     * (one we created earlier in this function on the first call,
     * the bootstrap progress UI's, or PPU's). If none exists yet —
     * which is the common case on warm launch when this is the
     * first paint of the run — create one here so the window stops
     * being a blank black rectangle for the ~1.4 s it would
     * otherwise take to reach Port_PPU_Init. PPU later adopts this
     * same renderer via SDL_GetRenderer(window). */
    SDL_Renderer* renderer = SDL_GetRenderer(window);
    if (!renderer) {
        /* Fallback: only triggers if main forgot to use
         * SDL_CreateWindowAndRenderer. Recreating the renderer
         * here on Linux/X11 forces SDL to re-add SDL_WINDOW_OPENGL
         * to the window, which destroys and recreates the X11
         * window — visible to the user as "first window closes,
         * second opens." Keep main's atomic-create path. */
        renderer = SDL_CreateRenderer(window, nullptr);
    }
    if (!renderer) return;

    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);
    const float fw = static_cast<float>(width);
    const float fh = static_cast<float>(height);
    const float scale = std::max(2.0f, std::round(fw / 240.0f));

    SDL_SetRenderDrawColor(renderer, 12, 16, 22, 255);
    SDL_RenderClear(renderer);

    const std::string text = message ? std::string(message) : std::string("STARTING");
    const float w = MeasureText(text, scale);
    DrawText(renderer, text, (fw - w) * 0.5f, (fh - 7.0f * scale) * 0.5f, scale);

    SDL_RenderPresent(renderer);
}
