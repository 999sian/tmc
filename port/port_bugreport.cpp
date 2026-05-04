/*
 * port_bugreport.cpp — F9-triggered bug-report capture for playtesters.
 *
 * Bundles screenshot + save file + game state into a timestamped directory
 * so testers can attach it to GitHub issues without needing to gather logs
 * themselves.
 */

#include "port_bugreport.h"

#include <SDL3/SDL.h>
#include <virtuappu.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

extern "C" {
extern uint8_t* gRomData;
extern uint32_t gRomSize;
extern uint32_t virtuappu_frame_buffer[];
}

/* Game-side globals we read for state.txt. Pulled in via extern. */
extern "C" {
struct PortBugReportState {
    uint8_t area;
    uint8_t room;
    int16_t playerX;
    int16_t playerY;
    int16_t playerZ;
    uint8_t playerHealth;
    uint8_t playerMaxHealth;
};
PortBugReportState Port_BugReport_GetGameState(void);
}

namespace {

std::string TimestampString() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm_buf);
    return buf;
}

bool WriteScreenshotBMP(const std::filesystem::path& path) {
    /* virtuappu_frame_buffer is 240*160 ABGR8888 pixels. */
    SDL_Surface* surf = SDL_CreateSurfaceFrom(240, 160, SDL_PIXELFORMAT_ABGR8888,
                                              virtuappu_frame_buffer,
                                              240 * static_cast<int>(sizeof(uint32_t)));
    if (!surf) {
        std::fprintf(stderr, "[BUG] CreateSurfaceFrom failed: %s\n", SDL_GetError());
        return false;
    }
    bool ok = SDL_SaveBMP(surf, path.string().c_str());
    SDL_DestroySurface(surf);
    if (!ok) {
        std::fprintf(stderr, "[BUG] SaveBMP failed: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

bool CopySaveFile(const std::filesystem::path& dest) {
    /* The PC port writes its EEPROM dump to "tmc.sav" relative to the cwd
     * (see port_save.c). Copy whatever's there. */
    std::error_code ec;
    std::filesystem::copy_file("tmc.sav", dest,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        std::fprintf(stderr, "[BUG] copy save: %s\n", ec.message().c_str());
        return false;
    }
    return true;
}

bool WriteStateText(const std::filesystem::path& path,
                    const PortBugReportState& s) {
    std::ofstream out(path);
    if (!out) {
        return false;
    }
    out << "TMC PC port bug report\n";
    out << "------\n";
    out << "Area:     0x" << std::hex << static_cast<unsigned>(s.area) << "\n";
    out << "Room:     0x" << std::hex << static_cast<unsigned>(s.room) << std::dec << "\n";
    out << "Pos:      (" << s.playerX << ", " << s.playerY << ", " << s.playerZ << ")\n";
    out << "HP:       " << static_cast<unsigned>(s.playerHealth) << " / "
        << static_cast<unsigned>(s.playerMaxHealth) << "\n";
    out << "ROM size: " << gRomSize << " bytes\n";
    out << "\n";
    out << "Please attach:\n";
    out << "  * screenshot.bmp  (what was on screen)\n";
    out << "  * save.bin         (your save state)\n";
    out << "  * a description of what you were trying to do\n";
    return out.good();
}

} // namespace

extern "C" char* Port_BugReport_Capture(void) {
    const std::string ts = TimestampString();
    const std::string dirname = "bugreport_" + ts;

    std::error_code ec;
    std::filesystem::create_directory(dirname, ec);
    if (ec) {
        std::fprintf(stderr, "[BUG] mkdir failed: %s\n", ec.message().c_str());
        return nullptr;
    }

    PortBugReportState s = Port_BugReport_GetGameState();

    bool ok = true;
    ok &= WriteScreenshotBMP(std::filesystem::path(dirname) / "screenshot.bmp");
    ok &= CopySaveFile(std::filesystem::path(dirname) / "save.bin");
    ok &= WriteStateText(std::filesystem::path(dirname) / "state.txt", s);

    std::fprintf(stderr, "[BUG] Captured %s (ok=%d)\n", dirname.c_str(), ok ? 1 : 0);

    char* out = static_cast<char*>(std::malloc(dirname.size() + 1));
    if (out) {
        std::memcpy(out, dirname.c_str(), dirname.size() + 1);
    }
    return out;
}
