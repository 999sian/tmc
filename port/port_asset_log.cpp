#include "port_asset_log.hpp"

#include <fmt/color.h>
#include <fmt/format.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <system_error>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace PortAssetLog {
namespace {

constexpr std::size_t kBarWidth = 28;
constexpr std::chrono::milliseconds kRedrawInterval{60};

bool IsStdoutTty()
{
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(fileno(stdout)) != 0;
#endif
}

bool EnableTerminalColors()
{
#ifdef _WIN32
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (handle == INVALID_HANDLE_VALUE || handle == nullptr) {
        return false;
    }
    DWORD mode = 0;
    if (!GetConsoleMode(handle, &mode)) {
        return false;
    }
    if ((mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) == 0) {
        if (!SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
            return false;
        }
    }
    SetConsoleOutputCP(CP_UTF8);
    return true;
#else
    const char* term = std::getenv("TERM");
    if (term == nullptr || std::strcmp(term, "dumb") == 0) {
        return false;
    }
    if (std::getenv("NO_COLOR") != nullptr) {
        return false;
    }
    return true;
#endif
}

std::string FormatDuration(std::chrono::steady_clock::duration dur)
{
    using namespace std::chrono;
    const auto millis = duration_cast<milliseconds>(dur).count();
    if (millis < 1000) {
        return fmt::format("{}ms", millis);
    }
    const double seconds = static_cast<double>(millis) / 1000.0;
    if (seconds < 60.0) {
        return fmt::format("{:.2f}s", seconds);
    }
    const long long total_seconds = static_cast<long long>(seconds);
    return fmt::format("{}m{:02d}s", total_seconds / 60, static_cast<int>(total_seconds % 60));
}

} // namespace

Reporter& Reporter::Instance()
{
    static Reporter instance;
    return instance;
}

Reporter::Reporter()
{
    tty_ = IsStdoutTty();
    color_ = tty_ && EnableTerminalColors();
}

void Reporter::SetVerbose(bool verbose)
{
    verbose_.store(verbose, std::memory_order_relaxed);
}

void Reporter::SetProgressCallback(ProgressCallback cb)
{
    std::lock_guard<std::mutex> lk(mutex_);
    progress_cb_ = std::move(cb);
    has_progress_cb_.store(static_cast<bool>(progress_cb_), std::memory_order_release);
}

void Reporter::FireProgressLocked()
{
    if (!progress_cb_) {
        return;
    }
    /* Snapshot the counters under the Reporter mutex so the
     * callback sees a consistent (phase, done, total). The callback
     * itself runs while the lock is held — embedders MUST keep it
     * cheap (the contract is "store into atomics and return"). */
    const std::size_t done = std::min(count_.load(std::memory_order_relaxed), total_);
    progress_cb_(std::string_view(phase_), done, total_);
}

void Reporter::ClearLineLocked()
{
    if (!tty_ || !line_dirty_) {
        return;
    }
    fmt::print("\r\x1b[2K");
    line_dirty_ = false;
}

std::string Reporter::FormatBarLocked(double fraction) const
{
    fraction = std::clamp(fraction, 0.0, 1.0);
    const std::size_t filled = static_cast<std::size_t>(fraction * kBarWidth + 0.5);
    std::string bar;
    bar.reserve(kBarWidth + 2);
    bar.push_back('[');
    for (std::size_t i = 0; i < kBarWidth; ++i) {
        if (i < filled) {
            bar.push_back('#');
        } else if (i == filled && filled < kBarWidth) {
            bar.push_back('>');
        } else {
            bar.push_back(' ');
        }
    }
    bar.push_back(']');
    return bar;
}

void Reporter::BeginPhase(std::string_view name, std::size_t total)
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (phase_active_) {
        ClearLineLocked();
    }
    phase_ = std::string(name);
    total_ = total;
    count_.store(0, std::memory_order_relaxed);
    phase_start_ = std::chrono::steady_clock::now();
    last_redraw_ = phase_start_ - kRedrawInterval;
    phase_active_ = true;
    RedrawLocked(true);
    FireProgressLocked();
}

void Reporter::Tick(std::size_t delta)
{
    if (delta == 0) {
        return;
    }
    count_.fetch_add(delta, std::memory_order_relaxed);

    /* Fast path: if there's no TTY redraw to perform AND no GUI
     * callback registered, we can skip the lock entirely. The GUI
     * callback must observe progress though, so an embedder with a
     * registered callback always falls through. */
    if (!tty_ && !has_progress_cb_.load(std::memory_order_acquire)) {
        return;
    }

    std::unique_lock<std::mutex> lk(mutex_, std::try_to_lock);
    if (!lk.owns_lock() || !phase_active_) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - last_redraw_ < kRedrawInterval) {
        return;
    }
    RedrawLocked(false);
    FireProgressLocked();
}

void Reporter::RedrawLocked(bool force)
{
    if (!phase_active_) {
        return;
    }

    const std::size_t count = std::min(count_.load(std::memory_order_relaxed), total_);
    const auto now = std::chrono::steady_clock::now();
    if (!force && tty_ && now - last_redraw_ < kRedrawInterval) {
        return;
    }
    last_redraw_ = now;

    if (!tty_) {
        return;
    }

    const double fraction = total_ == 0 ? 1.0 : static_cast<double>(count) / static_cast<double>(total_);
    const std::string bar = FormatBarLocked(fraction);
    const std::string elapsed = FormatDuration(now - phase_start_);

    std::string line;
    if (color_) {
        line = fmt::format("\r\x1b[2K\x1b[36m{:<18}\x1b[0m \x1b[32m{}\x1b[0m \x1b[1m{:>3.0f}%\x1b[0m  "
                           "{:>6}/{:<6} \x1b[2m{}\x1b[0m",
                           phase_, bar, fraction * 100.0, count, total_, elapsed);
    } else {
        line = fmt::format("\r{:<18} {} {:>3.0f}%  {:>6}/{:<6} {}", phase_, bar, fraction * 100.0, count,
                           total_, elapsed);
    }
    fmt::print("{}", line);
    std::fflush(stdout);
    line_dirty_ = true;
}

void Reporter::EndPhase()
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (!phase_active_) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const std::size_t count = std::min(count_.load(std::memory_order_relaxed), total_);
    const std::string elapsed = FormatDuration(now - phase_start_);

    if (tty_) {
        ClearLineLocked();
        if (color_) {
            fmt::print("\x1b[32m  ok\x1b[0m  \x1b[36m{:<18}\x1b[0m \x1b[2m{:>6} files in {}\x1b[0m\n", phase_,
                       total_, elapsed);
        } else {
            fmt::print("  ok  {:<18} {:>6} files in {}\n", phase_, total_, elapsed);
        }
    } else {
        fmt::print("[ok]  {:<18} {:>6} files in {}\n", phase_, total_, elapsed);
    }
    std::fflush(stdout);

    /* Fire one final progress event so embedders see done == total
     * before phase_active_ flips off (no further Ticks will land). */
    count_.store(total_, std::memory_order_relaxed);
    FireProgressLocked();

    phase_active_ = false;
    line_dirty_ = false;
    (void)count;
}

void Reporter::Note(std::string_view msg)
{
    std::lock_guard<std::mutex> lk(mutex_);
    ClearLineLocked();
    if (color_) {
        fmt::print("\x1b[2m  ..  {}\x1b[0m\n", msg);
    } else {
        fmt::print("  ..  {}\n", msg);
    }
    std::fflush(stdout);
    if (phase_active_) {
        RedrawLocked(true);
    }
}

void Reporter::Warn(std::string_view msg)
{
    std::lock_guard<std::mutex> lk(mutex_);
    ClearLineLocked();
    if (color_) {
        fmt::print("\x1b[33m warn  {}\x1b[0m\n", msg);
    } else {
        fmt::print(" warn  {}\n", msg);
    }
    std::fflush(stdout);
    if (phase_active_) {
        RedrawLocked(true);
    }
}

void Reporter::Error(std::string_view msg)
{
    std::lock_guard<std::mutex> lk(mutex_);
    ClearLineLocked();
    if (color_) {
        fmt::print(stderr, "\x1b[31m fail  {}\x1b[0m\n", msg);
    } else {
        fmt::print(stderr, " fail  {}\n", msg);
    }
    std::fflush(stderr);
    if (phase_active_) {
        RedrawLocked(true);
    }
}

void Reporter::Finish(std::string_view msg)
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (phase_active_) {
        ClearLineLocked();
        phase_active_ = false;
    }
    if (!msg.empty()) {
        if (color_) {
            fmt::print("\x1b[1;32mdone\x1b[0m  {}\n", msg);
        } else {
            fmt::print("done  {}\n", msg);
        }
        std::fflush(stdout);
    }
}

namespace {
std::mutex g_ensure_dir_mu;
std::unordered_set<std::string> g_ensured_dirs;
} // namespace

void EnsureDir(const std::filesystem::path& dir)
{
    if (dir.empty()) {
        return;
    }
    /* Hold the mutex across create_directories to avoid a race where one
     * thread inserts the dir into the cache, another thread looks it up,
     * sees it's "known", and immediately tries to write into it - but the
     * actual mkdir hasn't happened yet. The number of unique parent dirs
     * is small (a few hundred) so the contention is irrelevant compared to
     * the cost of the mkdir itself. */
    std::string key = dir.generic_string();
    std::lock_guard<std::mutex> lk(g_ensure_dir_mu);
    if (!g_ensured_dirs.insert(std::move(key)).second) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
}

void ResetEnsureDirCache()
{
    std::lock_guard<std::mutex> lk(g_ensure_dir_mu);
    g_ensured_dirs.clear();
}

struct BackgroundWriter::Task {
    std::filesystem::path path;
    nlohmann::json json;
    int indent;
};

BackgroundWriter& BackgroundWriter::Instance()
{
    static BackgroundWriter instance;
    return instance;
}

BackgroundWriter::BackgroundWriter()
{
    worker_ = std::thread(&BackgroundWriter::WorkerMain, this);
}

BackgroundWriter::~BackgroundWriter()
{
    Shutdown();
}

void BackgroundWriter::Submit(std::filesystem::path output_path, nlohmann::json json, int indent)
{
    Task task{std::move(output_path), std::move(json), indent};
    {
        std::lock_guard<std::mutex> lk(mutex_);
        queue_.push_back(std::move(task));
        ++in_flight_;
    }
    cv_.notify_one();
}

void BackgroundWriter::Wait()
{
    std::unique_lock<std::mutex> lk(mutex_);
    drain_cv_.wait(lk, [this]() { return in_flight_ == 0; });
    if (first_error_) {
        std::exception_ptr err = first_error_;
        first_error_ = nullptr;
        std::rethrow_exception(err);
    }
}

void BackgroundWriter::Shutdown()
{
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (stop_) {
            return;
        }
        stop_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void BackgroundWriter::WorkerMain()
{
    /* Buffer matches the inline write_text_buffered helper in the
     * extractor — fat enough to coalesce multi-MB JSON dumps into a
     * handful of write() syscalls. Reused across tasks; vector keeps
     * the allocation alive for the lifetime of the worker. */
    static constexpr std::streamsize kBuf = 256 * 1024;
    std::vector<char> file_buf(static_cast<std::size_t>(kBuf));

    for (;;) {
        Task task;
        {
            std::unique_lock<std::mutex> lk(mutex_);
            cv_.wait(lk, [this]() { return stop_ || !queue_.empty(); });
            if (queue_.empty()) {
                if (stop_) {
                    return;
                }
                continue;
            }
            task = std::move(queue_.front());
            queue_.erase(queue_.begin());
        }

        try {
            EnsureDir(task.path.parent_path());
            std::string serialized =
                task.indent > 0 ? task.json.dump(task.indent) : task.json.dump();
            std::ofstream f;
            f.rdbuf()->pubsetbuf(file_buf.data(), kBuf);
            f.open(task.path, std::ios::binary);
            if (!f) {
                throw std::runtime_error(
                    fmt::format("BackgroundWriter: failed to open {} for writing",
                                task.path.string()));
            }
            f.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
            f.flush();
            if (!f) {
                throw std::runtime_error(
                    fmt::format("BackgroundWriter: failed to write {}", task.path.string()));
            }
        } catch (...) {
            std::lock_guard<std::mutex> lk(mutex_);
            if (!first_error_) {
                first_error_ = std::current_exception();
            }
        }

        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (in_flight_ > 0) {
                --in_flight_;
            }
            if (in_flight_ == 0) {
                drain_cv_.notify_all();
            }
        }
    }
}

std::size_t WorkerCount()
{
    static const std::size_t cached = []() {
        unsigned hw = std::thread::hardware_concurrency();
        if (hw == 0) {
            hw = 4;
        }
        if (hw < 2) {
            hw = 2;
        }
        if (hw > 16) {
            hw = 16;
        }
        if (const char* env = std::getenv("TMC_ASSET_JOBS")) {
            int parsed = std::atoi(env);
            if (parsed > 0 && parsed <= 64) {
                return static_cast<std::size_t>(parsed);
            }
        }
        return static_cast<std::size_t>(hw);
    }();
    return cached;
}

} // namespace PortAssetLog
