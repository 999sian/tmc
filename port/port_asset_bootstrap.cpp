#include "port_asset_bootstrap.h"
#include "port_asset_pipeline.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <climits>
#include <mach-o/dyld.h>
#else
#include <climits>
#include <unistd.h>
#endif



constexpr const char* kExtractingMessage = "EXTRACTING ASSETS ...";

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
#else
    char buffer[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer));
    if (len > 0 && static_cast<size_t>(len) < sizeof(buffer)) {
        return std::filesystem::path(std::string(buffer, static_cast<size_t>(len))).parent_path();
    }
    return std::nullopt;
#endif
}

std::vector<std::filesystem::path> AssetSearchRoots() {
    std::vector<std::filesystem::path> roots;
    const auto exeDir = GetExecutableDirectory();
    if (exeDir.has_value()) {
        roots.push_back(*exeDir);
    }

    std::error_code ec;
    const auto cwd = std::filesystem::current_path(ec);
    if (!ec && (!exeDir.has_value() || cwd != *exeDir)) {
        roots.push_back(cwd);
    }

    return roots;
}

bool RuntimeAssetsReady(const std::filesystem::path& root) {
    const std::filesystem::path assets = root / "assets";
    return std::filesystem::exists(assets / "gfx_groups.json") &&
           std::filesystem::exists(assets / "palette_groups.json");
}

bool EditableAssetsReady(const std::filesystem::path& root) {
    const std::filesystem::path assets = root / "assets_src";
    return std::filesystem::exists(assets / "gfx_groups.json") &&
           std::filesystem::exists(assets / "palette_groups.json") &&
           std::filesystem::exists(assets / "palettes.json");
}

std::optional<std::filesystem::path> FindReadyRuntimeRoot() {
    for (const auto& root : AssetSearchRoots()) {
        if (RuntimeAssetsReady(root)) {
            return root;
        }
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> FindReadyEditableRoot() {
    for (const auto& root : AssetSearchRoots()) {
        if (EditableAssetsReady(root)) {
            return root;
        }
    }
    return std::nullopt;
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

std::filesystem::path AssetExtractorPath(const std::filesystem::path& root) {
#ifdef _WIN32
    return root / "asset_extractor.exe";
#else
    return root / "asset_extractor";
#endif
}

#ifdef _WIN32
std::string QuoteCommandPath(const std::filesystem::path& path) {
    std::string quoted = "\"";
    quoted += path.string();
    quoted += "\"";
    return quoted;
}
#else
std::string QuoteCommandPath(const std::filesystem::path& path) {
    std::string quoted = "'";
    for (char ch : path.string()) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}
#endif

bool RunAssetExtractor(const std::filesystem::path& root, std::string& error) {
    const std::filesystem::path extractor = AssetExtractorPath(root);
    if (!std::filesystem::exists(extractor)) {
        error = "asset_extractor was not found next to tmc_pc";
        return false;
    }

    const int status = std::system(QuoteCommandPath(extractor).c_str());
    if (status != 0) {
        error = "asset_extractor failed";
        return false;
    }

    if (!RuntimeAssetsReady(root)) {
        error = "asset_extractor finished but assets are still missing";
        return false;
    }

    return true;
}

bool BuildRuntimeAssetsFromEditable(const std::filesystem::path& root, std::string& error) {
    std::string pipelineError;
    if (!PortAssetPipeline::BuildRuntimeAssets(root / "assets_src", root / "assets", &pipelineError)) {
        error = pipelineError.empty() ? "failed to build runtime assets" : pipelineError;
        return false;
    }

    if (!RuntimeAssetsReady(root)) {
        error = "runtime assets were built but required manifests are still missing";
        return false;
    }

    return true;
}

using GlyphRows = std::array<unsigned char, 7>;

GlyphRows GlyphFor(char c) {
    switch (c) {
        case 'A': return { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
        case 'C': return { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E };
        case 'E': return { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F };
        case 'G': return { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F };
        case 'I': return { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F };
        case 'N': return { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 };
        case 'R': return { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 };
        case 'S': return { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E };
        case 'T': return { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };
        case 'X': return { 0x11, 0x0A, 0x04, 0x04, 0x04, 0x0A, 0x11 };
        case '.': return { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C };
        default: return { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    }
}

void DrawText(SDL_Renderer* renderer, const char* text, float x, float y, float scale) {
    SDL_SetRenderDrawColor(renderer, 235, 240, 245, 255);

    for (const char* p = text; *p; ++p) {
        const GlyphRows glyph = GlyphFor(*p);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if ((glyph[row] & (1 << (4 - col))) == 0) {
                    continue;
                }

                SDL_FRect pixel = { x + col * scale, y + row * scale, scale, scale };
                SDL_RenderFillRect(renderer, &pixel);
            }
        }
        x += (*p == ' ') ? scale * 4.0f : scale * 6.0f;
    }
}

void DrawExtractingScreen(SDL_Window* window, SDL_Renderer* renderer) {
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);

    const float scale = 4.0f;
    const float textWidth = 22.0f * 6.0f * scale;
    const float textHeight = 7.0f * scale;
    const float x = (static_cast<float>(width) - textWidth) * 0.5f;
    const float y = (static_cast<float>(height) - textHeight) * 0.5f;

    SDL_SetRenderDrawColor(renderer, 12, 16, 22, 255);
    SDL_RenderClear(renderer);
    DrawText(renderer, kExtractingMessage, x, y, scale);
    SDL_RenderPresent(renderer);
}

template <typename Task>
bool RunWithExtractingScreen(SDL_Window* window, Task task, std::string& error) {
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        return task();
    }

    auto future = std::async(std::launch::async, task);
    while (future.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                /* Keep extracting so the install is not left half-written. */
            }
        }
        DrawExtractingScreen(window, renderer);
        SDL_Delay(50);
    }

    DrawExtractingScreen(window, renderer);
    const bool ok = future.get();
    SDL_DestroyRenderer(renderer);
    (void)error;
    return ok;
}

extern "C" void Port_EnsureAssetsReadyWithDisplay(SDL_Window* window) {
    std::string error;
    bool ok = false;

    if (const auto editableRoot = FindReadyEditableRoot(); editableRoot.has_value()) {
        std::string reason;
        const bool needsBuild =
            !RuntimeAssetsReady(*editableRoot) ||
            PortAssetPipeline::RuntimeAssetsNeedRebuild(*editableRoot / "assets_src", *editableRoot / "assets", &reason);
        if (!needsBuild) {
            return;
        }

        ok = RunWithExtractingScreen(window,
                                     [&]() {
                                         return BuildRuntimeAssetsFromEditable(*editableRoot, error);
                                     },
                                     error);
    } else if (FindReadyRuntimeRoot().has_value()) {
        return;
    } else {
        const std::filesystem::path root = PreferredAssetRoot();
        ok = RunWithExtractingScreen(window,
                                     [&]() {
                                         return RunAssetExtractor(root, error);
                                     },
                                     error);
    }

    if (!ok) {
        const std::string message = error.empty() ? "Asset extraction failed." : error;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Asset extraction failed", message.c_str(), window);
    }
}
