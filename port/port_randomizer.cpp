#include "port_randomizer.h"

#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#if defined(__linux__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace {

const char* kCliBinaryNames[] = {
    "MinishCapRandomizerCLI",
#if defined(_WIN32)
    "MinishCapRandomizerCLI.exe",
#endif
    nullptr,
};

std::filesystem::path ExecutableDirectory() {
    char buf[4096];
#if defined(__linux__)
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

bool PathExists(const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::is_regular_file(p, ec);
}

std::filesystem::path SearchPath(const char* binary_name) {
    const char* path_env = std::getenv("PATH");
    if (!path_env) return {};
    std::string path = path_env;
#if defined(_WIN32)
    const char sep = ';';
#else
    const char sep = ':';
#endif
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find(sep, start);
        if (end == std::string::npos) end = path.size();
        std::filesystem::path candidate =
            std::filesystem::path(path.substr(start, end - start)) / binary_name;
        if (PathExists(candidate)) return candidate;
        if (end == path.size()) break;
        start = end + 1;
    }
    return {};
}

}  // namespace

extern "C" bool Port_Randomizer_FindCLI(char* out, size_t out_len) {
    if (!out || out_len == 0) return false;
    out[0] = '\0';

    /* 1. Explicit override */
    if (const char* override_path = std::getenv("TMC_RANDOMIZER_CLI")) {
        if (PathExists(override_path)) {
            std::strncpy(out, override_path, out_len - 1);
            out[out_len - 1] = '\0';
            return true;
        }
    }

    /* 2. <exe-dir>/randomizer/<binary> */
    const std::filesystem::path exeDir = ExecutableDirectory();
    for (int i = 0; kCliBinaryNames[i]; ++i) {
        const std::filesystem::path candidate = exeDir / "randomizer" / kCliBinaryNames[i];
        if (PathExists(candidate)) {
            const std::string s = candidate.string();
            std::strncpy(out, s.c_str(), out_len - 1);
            out[out_len - 1] = '\0';
            return true;
        }
    }

    /* 3. PATH lookup */
    for (int i = 0; kCliBinaryNames[i]; ++i) {
        const std::filesystem::path found = SearchPath(kCliBinaryNames[i]);
        if (!found.empty()) {
            const std::string s = found.string();
            std::strncpy(out, s.c_str(), out_len - 1);
            out[out_len - 1] = '\0';
            return true;
        }
    }

    return false;
}

extern "C" PortRandomizerStatus Port_Randomizer_RollSeed(
    const char* input_rom_path,
    uint32_t seed,
    const char* output_rom_path,
    const char* spoiler_path,
    char* error_out,
    size_t error_len) {

    auto report = [&](const char* msg) {
        if (error_out && error_len > 0) {
            std::strncpy(error_out, msg, error_len - 1);
            error_out[error_len - 1] = '\0';
        }
        std::fprintf(stderr, "[RANDO] %s\n", msg);
    };

    if (!input_rom_path || !output_rom_path) {
        report("missing required path argument");
        return PORT_RANDO_RUN_FAILED;
    }
    if (!PathExists(input_rom_path)) {
        report("input EU ROM not found at the given path");
        return PORT_RANDO_INPUT_ROM_MISSING;
    }

    char cli_path[4096];
    if (!Port_Randomizer_FindCLI(cli_path, sizeof(cli_path))) {
        report("randomizer CLI not found — set TMC_RANDOMIZER_CLI or place "
               "MinishCapRandomizerCLI under <exe>/randomizer/");
        return PORT_RANDO_CLI_NOT_FOUND;
    }

    /* If the caller passed seed==0, generate one ourselves so the
     * spoiler log echoes a value the user can re-roll. */
    if (seed == 0) {
        std::random_device rd;
        seed = (rd() & 0x7FFFFFFFu);
        if (seed == 0) seed = 1;
    }

    /* Build a temporary Commands.txt the CLI consumes. Format is
     * space-separated (NOT parenthesized) — see
     * libs/randomizer/MinishCapRandomizerCLI/CommandFileParser.cs:19,
     * which Split(' ')s the line and switches on inputs[0]. The earlier
     * `(path)` form silently produced an unrandomized ROM.
     *
     *   LoadRom <path>
     *   ChangeSeed S <seed>     ← "S" = set seed (vs "R" = random)
     *   Randomize 1             ← required; 1 = single attempt
     *   SaveRom <path>
     *   SaveSpoiler <path>      ← optional
     *   Exit
     */
    const std::filesystem::path exeDir = ExecutableDirectory();
    const std::filesystem::path cmd_file = exeDir / "tmc_rando_commands.txt";
    {
        std::ofstream out(cmd_file);
        if (!out.good()) {
            report("could not write temporary commands file");
            return PORT_RANDO_RUN_FAILED;
        }
        out << "LoadRom "    << input_rom_path  << "\n";
        out << "ChangeSeed S " << seed          << "\n";
        out << "Randomize 1\n";
        out << "SaveRom "    << output_rom_path << "\n";
        if (spoiler_path && spoiler_path[0]) {
            out << "SaveSpoiler " << spoiler_path << "\n";
        }
        out << "Exit\n";
    }

    /* Shell out. system() returns the CLI's exit code (with platform-
     * specific encoding); 0 = success, nonzero = failure. */
    std::string cli_cmd;
    cli_cmd.reserve(strlen(cli_path) + cmd_file.string().size() + 8);
    cli_cmd += '"';
    cli_cmd += cli_path;
    cli_cmd += "\" \"";
    cli_cmd += cmd_file.string();
    cli_cmd += '"';

    std::fprintf(stderr, "[RANDO] invoking: %s\n", cli_cmd.c_str());
    const int rc = std::system(cli_cmd.c_str());

    std::error_code ec;
    std::filesystem::remove(cmd_file, ec);

    if (rc != 0) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "CLI exited with code %d — check stderr for the CLI's own log",
                      rc);
        report(buf);
        return PORT_RANDO_RUN_FAILED;
    }
    if (!PathExists(output_rom_path)) {
        report("CLI reported success but no output ROM appeared at the target path");
        return PORT_RANDO_OUTPUT_MISSING;
    }

    char buf[256];
    std::snprintf(buf, sizeof(buf), "rolled seed %u → %s", seed, output_rom_path);
    report(buf);
    return PORT_RANDO_OK;
}
