#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace PortAssetLog {

class Reporter
{
  public:
    static Reporter& Instance();

    void SetVerbose(bool verbose);
    bool Verbose() const { return verbose_.load(std::memory_order_relaxed); }

    /* GUI progress hook for embedders (tmc_pc draws a real progress
     * bar from this). Fires whenever the in-process counters change:
     *   - BeginPhase: done=0, total=N, phase=name
     *   - Tick:       done=current, total=N, phase=current name
     *                 (throttled to the same kRedrawInterval as the TTY redraw)
     *   - EndPhase:   done=total, total=N, phase=name (one final fire)
     * Called from worker threads under the Reporter mutex; the
     * callback should snapshot into atomics and return quickly. */
    using ProgressCallback =
        std::function<void(std::string_view phase, std::size_t done, std::size_t total)>;
    void SetProgressCallback(ProgressCallback cb);

    void BeginPhase(std::string_view name, std::size_t total);
    void Tick(std::size_t delta = 1);
    void EndPhase();

    void Note(std::string_view msg);
    void Warn(std::string_view msg);
    void Error(std::string_view msg);

    void Finish(std::string_view msg = {});

  private:
    Reporter();
    Reporter(const Reporter&) = delete;
    Reporter& operator=(const Reporter&) = delete;

    void RedrawLocked(bool force);
    void ClearLineLocked();
    std::string FormatBarLocked(double fraction) const;

    void FireProgressLocked();

    std::mutex mutex_;
    std::atomic<bool> verbose_{false};
    bool tty_{false};
    bool color_{false};

    std::string phase_;
    std::size_t total_{0};
    std::atomic<std::size_t> count_{0};
    std::chrono::steady_clock::time_point phase_start_{};
    std::chrono::steady_clock::time_point last_redraw_{};
    bool phase_active_{false};
    bool line_dirty_{false};

    ProgressCallback progress_cb_;
    /* Lock-free hint so Tick's fast path can short-circuit when no
     * embedder needs progress events (the common standalone case).
     * Accessed without holding mutex_; safe because flips are rare
     * (set once at the start of an extraction run). */
    std::atomic<bool> has_progress_cb_{false};
};

std::size_t WorkerCount();

/* BackgroundWriter
 *
 * Single-worker thread that owns serialisation + write of pretty-printed
 * JSON. The extractor's hot phases (parallel-fanout over palettes, gfx,
 * tilemaps, sprites, areas, texts) each end with a multi-megabyte JSON
 * dump(4) + ofstream::write — both slow, both serial. Submitting them
 * here lets the main thread immediately start the next phase while the
 * pretty-printer runs concurrently.
 *
 * One worker (not WorkerCount()) on purpose: aggregate JSON dumps run
 * one-at-a-time anyway (we only have ~9 of them total), and a single
 * worker means at most one pending serialisation in flight, bounding
 * peak memory.
 *
 * Submit() takes the JSON by value (callers can std::move into it) and
 * the indent (typically 4 for assets_src/, 0 for assets/). Wait()
 * blocks until the queue is drained — call before WriteBuildStateFile
 * so the build state observes the final on-disk shape.
 *
 * Errors are captured per-task and rethrown from Wait(), the same
 * "first error wins, all tasks complete" semantics as ParallelFor. */
class BackgroundWriter
{
  public:
    static BackgroundWriter& Instance();

    /* Submit a pretty-printed JSON dump + write. indent>0 = pretty
     * (assets_src/), indent==0 = compact (assets/). The path's parent
     * directory is ensured via EnsureDir on the worker. */
    void Submit(std::filesystem::path output_path, nlohmann::json json, int indent);

    /* Block until the queue is drained. Safe to call multiple times.
     * Re-throws the first exception encountered on the worker, if any. */
    void Wait();

    /* Idle the worker permanently. Called automatically at process exit
     * via static destruction; safe to call manually too. */
    void Shutdown();

  private:
    BackgroundWriter();
    ~BackgroundWriter();
    BackgroundWriter(const BackgroundWriter&) = delete;
    BackgroundWriter& operator=(const BackgroundWriter&) = delete;

    void WorkerMain();

    struct Task;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable drain_cv_;
    std::vector<Task> queue_;
    std::size_t in_flight_{0};
    bool stop_{false};
    std::exception_ptr first_error_;
    std::thread worker_;
};

// Thread-safe cache around std::filesystem::create_directories. Each unique
// directory triggers exactly one create_directories call. Safe to call from
// any thread; subsequent calls for already-known dirs are a hash lookup.
void EnsureDir(const std::filesystem::path& dir);

// Reset the EnsureDir cache. Useful between independent extraction runs in
// the same process (otherwise unused).
void ResetEnsureDirCache();

template <typename Index, typename Fn>
void ParallelFor(Index begin, Index end, Fn body)
{
    static_assert(std::is_integral_v<Index>, "ParallelFor requires an integral index type");
    if (end <= begin) {
        return;
    }

    const std::size_t total = static_cast<std::size_t>(end - begin);
    const std::size_t workers = std::min<std::size_t>(WorkerCount(), total);
    if (workers <= 1) {
        for (Index i = begin; i < end; ++i) {
            body(i);
        }
        return;
    }

    std::atomic<std::size_t> next{0};
    std::mutex error_mu;
    std::exception_ptr first_error;

    auto worker = [&]() {
        try {
            for (;;) {
                const std::size_t i = next.fetch_add(1, std::memory_order_relaxed);
                if (i >= total) {
                    return;
                }
                body(begin + static_cast<Index>(i));
            }
        } catch (...) {
            std::lock_guard<std::mutex> lk(error_mu);
            if (!first_error) {
                first_error = std::current_exception();
            }
            next.store(total, std::memory_order_relaxed);
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(workers - 1);
    for (std::size_t t = 1; t < workers; ++t) {
        threads.emplace_back(worker);
    }
    worker();
    for (auto& th : threads) {
        th.join();
    }

    if (first_error) {
        std::rethrow_exception(first_error);
    }
}

} // namespace PortAssetLog
