#include "util.h"
#include "simple_format.h"
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

extern "C" int gbagfx_main(int argc, char** argv);
extern "C" int aif2pcm_main(int argc, char** argv);

// The shell used to strip one level of quotes (json string options are passed
// as "value"); callers that bypass it do the same.
static std::vector<std::string> unquoted(const std::vector<std::string>& cmd) {
    std::vector<std::string> args;
    args.reserve(cmd.size());
    for (const auto& arg : cmd) {
        if (arg.size() >= 2 && arg.front() == '"' && arg.back() == '"') {
            args.push_back(arg.substr(1, arg.size() - 2));
        } else {
            args.push_back(arg);
        }
    }
    return args;
}

// Run a converter directly instead of through system(): no shell process per
// call (on Windows that was a cmd.exe for each of ~20k conversions).
static int run_process(const std::vector<std::string>& cmd, const std::string& cmdstr) {
#ifdef _WIN32
    // Hand the child the same command line system() did; its CRT splits argv
    // exactly as before, quotes included.
    std::string exe = cmd[0];
    for (char& c : exe) {
        if (c == '/') {
            c = '\\';
        }
    }
    if (exe.find('.', exe.find_last_of('\\') + 1) == std::string::npos) {
        exe += ".exe";
    }
    std::vector<char> line(cmdstr.begin(), cmdstr.end());
    line.push_back('\0');
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(exe.c_str(), line.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
        return -1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
#else
    std::vector<std::string> args = unquoted(cmd);
    std::vector<char*> argv;
    for (auto& arg : args) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);
    pid_t pid;
    if (posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ) != 0) {
        return -1;
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

void check_call(const std::vector<std::string>& cmd) {
    std::string cmdstr;
    bool first = true;
    for (const auto& segment : cmd) {
        if (first) {
            first = false;
        } else {
            cmdstr += " ";
        }
        cmdstr += segment;
    }
    int code;
    const std::string tool = std::filesystem::path(cmd[0]).filename().string();
    if (tool == "gbagfx" || tool == "aif2pcm") {
        // Call the linked-in copy, no process launch (launches are slow and, on
        // Windows, serialized system-wide). Neither tool keeps global state; on
        // error they exit, as a failed launch would. mid2agb/agb2mid keep
        // per-run globals, so they still run as processes.
        std::vector<std::string> args = unquoted(cmd);
        std::vector<char*> argv;
        for (auto& arg : args) {
            argv.push_back(arg.data());
        }
        argv.push_back(nullptr);
        code = (tool == "gbagfx" ? gbagfx_main : aif2pcm_main)(static_cast<int>(args.size()), argv.data());
    } else {
        code = run_process(cmd, cmdstr);
    }
    if (code != 0) {
        std::cerr << cmdstr << " failed with return code " << code << std::endl;
        std::exit(1);
    }
}

std::string opt_param(const std::string& format, int defaultVal, int value) {
    if (value != defaultVal) {
        return assetfmt::Format(format, value);
    }
    return "";
}

std::string hex_u32(uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::nouppercase << value;
    return out.str();
}
