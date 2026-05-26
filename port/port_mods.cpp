#include "port_mods.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#if defined(__linux__)
#include <unistd.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

/* Forward decl: the asset loader owns the override-roots vector and
 * appends to it as part of its singleton init. Defined in
 * port_asset_loader.cpp. */
extern void Port_AddModRoot(const std::filesystem::path& modRoot);

namespace {

std::filesystem::path ExecutableDirectory() {
    /* Mirrors port_asset_bootstrap.cpp::GetExecutableDirectory. Kept
     * local to avoid pulling in port_asset_bootstrap.hpp's full deps.
     * Each platform has a different API for "where is my exe":
     *   Linux  → readlink("/proc/self/exe")
     *   macOS  → _NSGetExecutablePath  (+ weakly_canonical to resolve symlinks)
     *   Windows → GetModuleFileNameW
     * The cwd fallback is for unusual layouts (e.g. someone running a
     * launch wrapper) where the platform query fails. */
#if defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, buffer.data(),
                                   static_cast<DWORD>(buffer.size()));
    while (len > 0 && len >= buffer.size() - 1) {
        buffer.resize(buffer.size() * 2);
        len = GetModuleFileNameW(nullptr, buffer.data(),
                                 static_cast<DWORD>(buffer.size()));
    }
    if (len > 0) {
        buffer.resize(len);
        return std::filesystem::path(buffer).parent_path();
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buf2(size, '\0');
    if (_NSGetExecutablePath(buf2.data(), &size) == 0) {
        std::error_code ec;
        auto canonical = std::filesystem::weakly_canonical(buf2.c_str(), ec);
        if (!ec) return canonical.parent_path();
    }
#elif defined(__linux__)
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf));
    if (len > 0 && static_cast<size_t>(len) < sizeof(buf)) {
        buf[len] = '\0';
        return std::filesystem::path(buf).parent_path();
    }
#endif
    std::error_code ec;
    auto cwd = std::filesystem::current_path(ec);
    return ec ? std::filesystem::path(".") : cwd;
}

std::vector<std::string> SplitCsv(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else if (c != ' ' && c != '\t') {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

}  // namespace

extern "C" void Port_Mods_Init(void) {
    const std::filesystem::path exeDir = ExecutableDirectory();
    const std::filesystem::path modsRoot = exeDir / "mods";

    std::error_code ec;
    if (!std::filesystem::is_directory(modsRoot, ec)) {
        return; /* No mods/ folder — nothing to load. */
    }

    /* TMC_MODS=foo,bar — explicit ordered list overrides auto-discovery. */
    std::vector<std::string> chosen;
    if (const char* env = SDL_getenv("TMC_MODS")) {
        chosen = SplitCsv(env);
    } else {
        for (const auto& entry : std::filesystem::directory_iterator(modsRoot, ec)) {
            if (entry.is_directory(ec)) {
                const std::string name = entry.path().filename().string();
                if (!name.empty() && name[0] != '.') {
                    chosen.push_back(name);
                }
            }
        }
        std::sort(chosen.begin(), chosen.end());
    }

    if (chosen.empty()) {
        std::fprintf(stderr, "[MOD] mods/ exists but no active mods (TMC_MODS unset, dir empty)\n");
        return;
    }

    int registered = 0;
    for (const auto& name : chosen) {
        const std::filesystem::path modDir = modsRoot / name;
        if (!std::filesystem::is_directory(modDir, ec)) {
            std::fprintf(stderr, "[MOD] skipping '%s' — not a directory\n", name.c_str());
            continue;
        }
        Port_AddModRoot(modDir);
        std::fprintf(stderr, "[MOD] active: %s -> %s\n", name.c_str(), modDir.string().c_str());
        registered++;
    }
    std::fprintf(stderr, "[MOD] %d mod%s registered\n", registered, registered == 1 ? "" : "s");
}
