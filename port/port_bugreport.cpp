/*
 * port_bugreport.cpp — F9-triggered + auto-on-crash bug-report capture.
 *
 * Bundles screenshot (PNG) + save file + game state into a timestamped
 * directory so testers can attach it to GitHub issues without needing to
 * gather logs themselves. The crash handler hooks SIGSEGV/SIGABRT/SIGFPE/
 * SIGILL/SIGBUS (POSIX) and SetUnhandledExceptionFilter (Windows), captures
 * a bundle plus a backtrace, then re-raises so the OS still produces its
 * default core dump / WER report.
 */

#include "port_bugreport.h"
#include "port_version.h"

#include <SDL3/SDL.h>
#include <png.h>
#include <virtuappu.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#if defined(__linux__) || defined(__APPLE__)
#include <cerrno>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/ucontext.h>
#include <sys/wait.h>
#endif

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#endif

extern "C" {
extern uint8_t* gRomData;
extern uint32_t gRomSize;
extern uint32_t virtuappu_frame_buffer[];
}

extern "C" {
struct PortBugReportState {
    uint8_t area;
    uint8_t room;
    int16_t playerX;
    int16_t playerY;
    int16_t playerZ;
    uint8_t playerHealth;
    uint8_t playerMaxHealth;
    int frameCount;
};
PortBugReportState Port_BugReport_GetGameState(void);
}

namespace {

constexpr int kFrameW = 240;
constexpr int kFrameH = 160;

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

const char* PlatformString() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

const char* ArchString() {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#elif defined(__i386__) || defined(_M_IX86)
    return "i686";
#else
    return "unknown";
#endif
}

const char* BuildModeString() {
#ifdef NDEBUG
    return "release";
#else
    return "debug";
#endif
}

const char* GameRegionString() {
#if defined(EU)
    return "EU";
#elif defined(USA)
    return "USA";
#elif defined(JP)
    return "JP";
#else
    return "?";
#endif
}

/* ---------- stderr ring buffer ----------
 *
 * dup2 stderr to a pipe, run a reader thread that:
 *   (a) appends every chunk into a fixed-size circular buffer, and
 *   (b) re-emits each chunk to the original fd 2 so the user still
 *       sees logs in real time.
 *
 * On bug-report capture we snapshot the ring and write it as
 * stderr.log alongside the rest of the bundle. This unblocks
 * diagnosing aborts whose only signal is on stderr (gba_MemPtr
 * "FATAL: invalid address 0x..." etc.) which previously vanished
 * with the terminal session.
 *
 * The dump path runs from the SIGSEGV/SIGABRT handler, so it must be
 * async-signal-safe: no malloc, no std::ofstream, no mutex. We use
 * atomic head + atomic wrap-flag (single-writer/single-reader still
 * means relaxed ordering is enough — only the reader thread writes,
 * and the dump just snapshots the indices) and write(2) directly to
 * a freshly opened fd. The ring's storage is statically sized so no
 * allocation happens anywhere on the dump path. */
constexpr size_t kStderrRingSize = 64 * 1024;

struct StderrRing {
    char buf[kStderrRingSize] {};
    std::atomic<size_t> head{0};        /* next write offset, [0, kStderrRingSize) */
    std::atomic<bool> wrapped{false};
    int orig_stderr_fd = -1;

    /* Called only from the reader thread, so writes are serialised. */
    void Append(const char* data, size_t len) {
        size_t h = head.load(std::memory_order_relaxed);
        while (len > 0) {
            size_t room = kStderrRingSize - h;
            size_t n = len < room ? len : room;
            std::memcpy(buf + h, data, n);
            h += n;
            if (h == kStderrRingSize) {
                h = 0;
                wrapped.store(true, std::memory_order_release);
            }
            data += n;
            len -= n;
        }
        head.store(h, std::memory_order_release);
    }

#if defined(__linux__) || defined(__APPLE__)
    /* Async-signal-safe dump. Path string must already be NUL-terminated.
     * Tiny race with the reader thread is acceptable — at worst we miss
     * the tail of a half-written chunk; the rest of the buffer is
     * already-committed data. */
    bool DumpTo(const char* path) {
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (fd < 0) return false;

        size_t h = head.load(std::memory_order_acquire);
        bool w = wrapped.load(std::memory_order_acquire);

        auto write_all = [fd](const char* p, size_t n) {
            while (n > 0) {
                ssize_t r = write(fd, p, n);
                if (r <= 0) {
                    if (r < 0 && errno == EINTR) continue;
                    return false;
                }
                p += r;
                n -= r;
            }
            return true;
        };

        bool ok = true;
        if (w) {
            /* Older portion: head .. end. Skip the leading partial line so
             * the file starts on a line boundary. */
            const char* tail_start = buf + h;
            size_t tail_len = kStderrRingSize - h;
            const void* nl = std::memchr(tail_start, '\n', tail_len);
            if (nl) {
                size_t skip = static_cast<const char*>(nl) - tail_start + 1;
                tail_start += skip;
                tail_len -= skip;
            } else {
                tail_len = 0;
            }
            ok = ok && write_all(tail_start, tail_len);
        }
        ok = ok && write_all(buf, h);
        close(fd);
        return ok;
    }
#endif
};

StderrRing g_stderrRing;
std::atomic<int> g_stderr_capture_installed{0};

#if defined(__linux__) || defined(__APPLE__)
void StderrReaderLoop(int read_fd, int orig_fd) {
    char chunk[4096];
    for (;;) {
        ssize_t n = read(read_fd, chunk, sizeof(chunk));
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            break;
        }
        g_stderrRing.Append(chunk, static_cast<size_t>(n));
        /* Mirror back to the real terminal so the user still sees logs.
         * Best-effort — if this fails (terminal closed) we keep capturing. */
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = write(orig_fd, chunk + off, n - off);
            if (w <= 0) {
                if (w < 0 && errno == EINTR) continue;
                break;
            }
            off += w;
        }
    }
}

bool InstallStderrCapturePosix() {
    int orig = dup(STDERR_FILENO);
    if (orig < 0) return false;
    /* Make the saved fd survive exec() in case anything fork+exec's later. */
    int flags = fcntl(orig, F_GETFD);
    if (flags >= 0) fcntl(orig, F_SETFD, flags | FD_CLOEXEC);

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        close(orig);
        return false;
    }
    /* Replace fd 2 with the pipe's write end so every write to stderr
     * (libc, third-party libs, raw write(2)) ends up in our reader. */
    if (dup2(pipefd[1], STDERR_FILENO) < 0) {
        close(orig);
        close(pipefd[0]);
        close(pipefd[1]);
        return false;
    }
    close(pipefd[1]);

    /* glibc's stderr is unbuffered by default; reaffirm in case the runtime
     * has changed buffering before we got here. */
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    g_stderrRing.orig_stderr_fd = orig;
    std::thread(StderrReaderLoop, pipefd[0], orig).detach();
    return true;
}
#endif

bool InstallStderrCapture() {
    int expected = 0;
    if (!g_stderr_capture_installed.compare_exchange_strong(expected, 1)) {
        return true;
    }
#if defined(__linux__) || defined(__APPLE__)
    return InstallStderrCapturePosix();
#else
    /* TODO Windows: CreatePipe + SetStdHandle + reader thread. The wine
     * Windows path will keep emitting straight to the console for now. */
    return false;
#endif
}

bool WriteScreenshotPNG(const std::filesystem::path& path) {
    /* virtuappu_frame_buffer is kFrameW*kFrameH ABGR8888 (little-endian: B,G,R,A
     * in memory). Convert to packed RGB8 row-by-row and hand to libpng. */
    FILE* fp = std::fopen(path.string().c_str(), "wb");
    if (!fp) {
        std::fprintf(stderr, "[BUG] PNG fopen failed: %s\n", path.string().c_str());
        return false;
    }

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        std::fclose(fp);
        return false;
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, nullptr);
        std::fclose(fp);
        return false;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        std::fclose(fp);
        std::fprintf(stderr, "[BUG] libpng longjmp\n");
        return false;
    }

    png_init_io(png, fp);
    png_set_IHDR(png, info, kFrameW, kFrameH, 8, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    uint8_t row[kFrameW * 3];
    for (int y = 0; y < kFrameH; y++) {
        const uint32_t* src = &virtuappu_frame_buffer[y * kFrameW];
        for (int x = 0; x < kFrameW; x++) {
            uint32_t p = src[x];
            row[x * 3 + 0] = static_cast<uint8_t>(p & 0xFF);          /* R (ABGR LE: byte0=R) */
            row[x * 3 + 1] = static_cast<uint8_t>((p >> 8) & 0xFF);   /* G */
            row[x * 3 + 2] = static_cast<uint8_t>((p >> 16) & 0xFF);  /* B */
        }
        png_write_row(png, row);
    }
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    std::fclose(fp);
    return true;
}

bool CopySaveFile(const std::filesystem::path& dest) {
    std::error_code ec;
    std::filesystem::copy_file("tmc.sav", dest,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        std::fprintf(stderr, "[BUG] copy save: %s\n", ec.message().c_str());
        return false;
    }
    return true;
}

bool CopyProcMaps(const std::filesystem::path& dest) {
    /* Snapshot /proc/self/maps so the crash IP in backtrace.txt can be
     * resolved offline (subtract binary load base, then addr2line on the
     * offset). The handler can't safely call dladdr to do this online —
     * /proc reads are async-signal-safe via plain syscalls. */
    std::error_code ec;
    std::filesystem::copy_file("/proc/self/maps", dest,
                               std::filesystem::copy_options::overwrite_existing, ec);
    return !ec;
}

bool WriteStateText(const std::filesystem::path& path,
                    const PortBugReportState& s,
                    const char* reason) {
    std::ofstream out(path);
    if (!out) {
        return false;
    }
    out << "TMC PC port bug report\n";
    out << "------\n";
    out << "Reason:    " << (reason && *reason ? reason : "user") << "\n";
    out << "Version:   " << TMC_PC_VERSION << " (" << GameRegionString() << ")\n";
    out << "Build:     " << BuildModeString() << " " << PlatformString() << "/" << ArchString() << "\n";
    out << "Area:      0x" << std::hex << static_cast<unsigned>(s.area) << "\n";
    out << "Room:      0x" << std::hex << static_cast<unsigned>(s.room) << std::dec << "\n";
    out << "Pos:       (" << s.playerX << ", " << s.playerY << ", " << s.playerZ << ")\n";
    out << "HP:        " << static_cast<unsigned>(s.playerHealth) << " / "
        << static_cast<unsigned>(s.playerMaxHealth) << "\n";
    out << "Frame:     " << s.frameCount << "\n";
    out << "ROM size:  " << gRomSize << " bytes\n";
    return out.good();
}

#if defined(__linux__) || defined(__APPLE__)
struct CrashRegs {
    void* ip;   /* RIP / PC at fault */
    void* sp;   /* RSP / SP at fault */
    void* bp;   /* RBP / FP at fault (may be omitted by -O3) */
    void* lr;   /* link register on aarch64; nullptr on x86-64 */
};

CrashRegs ExtractCrashRegs(void* ucontext) {
    CrashRegs r{};
    if (!ucontext) {
        return r;
    }
    auto* uc = static_cast<ucontext_t*>(ucontext);
#if defined(__linux__) && (defined(__x86_64__) || defined(_M_X64))
    r.ip = reinterpret_cast<void*>(uc->uc_mcontext.gregs[REG_RIP]);
    r.sp = reinterpret_cast<void*>(uc->uc_mcontext.gregs[REG_RSP]);
    r.bp = reinterpret_cast<void*>(uc->uc_mcontext.gregs[REG_RBP]);
#elif defined(__linux__) && defined(__aarch64__)
    r.ip = reinterpret_cast<void*>(uc->uc_mcontext.pc);
    r.sp = reinterpret_cast<void*>(uc->uc_mcontext.sp);
    r.lr = reinterpret_cast<void*>(uc->uc_mcontext.regs[30]);
#elif defined(__APPLE__) && defined(__x86_64__)
    r.ip = reinterpret_cast<void*>(uc->uc_mcontext->__ss.__rip);
    r.sp = reinterpret_cast<void*>(uc->uc_mcontext->__ss.__rsp);
    r.bp = reinterpret_cast<void*>(uc->uc_mcontext->__ss.__rbp);
#elif defined(__APPLE__) && defined(__aarch64__)
    r.ip = reinterpret_cast<void*>(uc->uc_mcontext->__ss.__pc);
    r.sp = reinterpret_cast<void*>(uc->uc_mcontext->__ss.__sp);
    r.lr = reinterpret_cast<void*>(uc->uc_mcontext->__ss.__lr);
#endif
    return r;
}

void* SafeReadPointer(void* p) {
    /* Read 8 bytes from *p without faulting if p is unmapped. We can't
     * trap a SEGV inside our SEGV handler reliably, so fall back to a
     * conservative null check. The stack pointer at the crash site is
     * (almost always) mapped so this is mostly belt-and-braces. */
    if (!p) return nullptr;
    void* v = nullptr;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

/* Resolve a code address (relative to the binary base) and write a one-line
 * description: `0xADDR <module>(+0xOFFSET) [symbol+disp]`. dladdr is
 * sufficient for the crash IP on glibc — for a full unwind, libunwind would
 * be needed. */
/* Resolve the path of the current executable into `buf` (NUL-terminated).
 * Returns true on success. Used for shelling out to addr2line. */
bool ResolveOwnExePath(char* buf, size_t buflen) {
    if (buflen < 2) return false;
#if defined(__linux__)
    ssize_t n = readlink("/proc/self/exe", buf, buflen - 1);
    if (n <= 0) return false;
    buf[n] = '\0';
    return true;
#elif defined(__APPLE__)
    /* _NSGetExecutablePath needs <mach-o/dyld.h>; the port's other call
     * sites use it but we keep this best-effort: if dladdr can give us
     * the binary path that's good enough for addr2line. */
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&ResolveOwnExePath), &info) && info.dli_fname) {
        std::strncpy(buf, info.dli_fname, buflen - 1);
        buf[buflen - 1] = '\0';
        return true;
    }
    return false;
#else
    (void)buf; (void)buflen;
    return false;
#endif
}

#if defined(__linux__) || defined(__APPLE__)
/* Shell out to addr2line for file:line resolution. addr2line accepts
 * binary-relative offsets when fed a PIE binary, so we subtract the load
 * base. fork+execvp+pipe+read+waitpid are in POSIX async-signal-safe set,
 * so this can run from the SIGSEGV handler. We avoid stdio in the parent
 * and pipe addr2line's stdout straight into the report fd. */
void RunAddr2Line(int report_fd, void** addrs, int n) {
    if (n <= 0) return;

    char exe_path[1024];
    if (!ResolveOwnExePath(exe_path, sizeof(exe_path))) return;

    Dl_info info{};
    if (!dladdr(reinterpret_cast<void*>(&RunAddr2Line), &info) || !info.dli_fbase) return;
    uintptr_t base = reinterpret_cast<uintptr_t>(info.dli_fbase);

    /* Build argv: addr2line -e <exe> -fpCi <off1> <off2> ... NULL.
     * -f: print function name
     * -p: pretty single-line format
     * -C: demangle C++
     * -i: walk inlined frames
     * Keep at most 32 addrs to bound the cmdline length. */
    constexpr int kMaxAddrs = 32;
    if (n > kMaxAddrs) n = kMaxAddrs;
    char addr_strs[kMaxAddrs][20];
    char* argv[5 + kMaxAddrs + 1];
    int ai = 0;
    argv[ai++] = const_cast<char*>("addr2line");
    argv[ai++] = const_cast<char*>("-e");
    argv[ai++] = exe_path;
    argv[ai++] = const_cast<char*>("-fpCi");
    for (int i = 0; i < n; i++) {
        uintptr_t a = reinterpret_cast<uintptr_t>(addrs[i]);
        if (a < base) { addr_strs[i][0] = '0'; addr_strs[i][1] = '\0'; }
        else std::snprintf(addr_strs[i], sizeof(addr_strs[i]), "0x%lx",
                           static_cast<unsigned long>(a - base));
        argv[ai++] = addr_strs[i];
    }
    argv[ai] = nullptr;

    int pipefd[2];
    if (pipe(pipefd) != 0) return;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]); close(pipefd[1]);
        return;
    }
    if (pid == 0) {
        /* Child. Redirect stdout to the pipe, close everything else. */
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execvp("addr2line", argv);
        _exit(127);
    }
    close(pipefd[1]);

    /* Header so the section is identifiable in the report file. */
    const char hdr[] = "\n--- addr2line (file:line) ---\n";
    (void)!write(report_fd, hdr, sizeof(hdr) - 1);

    char chunk[4096];
    for (;;) {
        ssize_t r = read(pipefd[0], chunk, sizeof(chunk));
        if (r <= 0) {
            if (r < 0 && errno == EINTR) continue;
            break;
        }
        ssize_t off = 0;
        while (off < r) {
            ssize_t w = write(report_fd, chunk + off, r - off);
            if (w <= 0) {
                if (w < 0 && errno == EINTR) continue;
                break;
            }
            off += w;
        }
    }
    close(pipefd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
}
#endif

void WriteResolvedAddr(FILE* fp, const char* label, void* addr) {
    Dl_info info{};
    if (dladdr(addr, &info) && info.dli_fname) {
        uintptr_t base = reinterpret_cast<uintptr_t>(info.dli_fbase);
        uintptr_t offs = reinterpret_cast<uintptr_t>(addr) - base;
        if (info.dli_sname) {
            uintptr_t sym_off = reinterpret_cast<uintptr_t>(addr)
                              - reinterpret_cast<uintptr_t>(info.dli_saddr);
            std::fprintf(fp, "%s 0x%lx %s(+0x%lx) [%s+0x%lx]\n",
                         label,
                         static_cast<unsigned long>(reinterpret_cast<uintptr_t>(addr)),
                         info.dli_fname,
                         static_cast<unsigned long>(offs),
                         info.dli_sname,
                         static_cast<unsigned long>(sym_off));
        } else {
            std::fprintf(fp, "%s 0x%lx %s(+0x%lx)\n",
                         label,
                         static_cast<unsigned long>(reinterpret_cast<uintptr_t>(addr)),
                         info.dli_fname,
                         static_cast<unsigned long>(offs));
        }
    } else {
        std::fprintf(fp, "%s 0x%lx (unmapped)\n",
                     label,
                     static_cast<unsigned long>(reinterpret_cast<uintptr_t>(addr)));
    }
}

void WriteBacktracePosix(const std::filesystem::path& path,
                         const CrashRegs& regs,
                         void* fault_addr) {
    FILE* fp = std::fopen(path.string().c_str(), "wb");
    if (!fp) {
        return;
    }

    /* Stage 1 — write raw hex addresses with fflush BEFORE *any* call
     * that's not async-signal-safe (no dladdr, no malloc, no anything
     * that might lock libc internals). Past attempts at "hardening"
     * still kicked off with a dladdr() to get the binary base for
     * offset reporting; that itself crashes inside SIGSEGV handling
     * and leaves the file empty. Skip offsets entirely in Stage 1 —
     * the absolute IP is enough for `addr2line -e tmc_pc <ip>` since
     * the binary is the only thing in the address range we care about. */
    if (regs.ip) {
        std::fprintf(fp, "Crash IP:    0x%lx\n",
                     static_cast<unsigned long>(reinterpret_cast<uintptr_t>(regs.ip)));
    } else {
        std::fprintf(fp, "Crash IP:    0x0 (program jumped to NULL — likely a NULL function-pointer call)\n");
    }
    std::fprintf(fp, "Fault addr:  0x%lx\n",
                 static_cast<unsigned long>(reinterpret_cast<uintptr_t>(fault_addr)));
    std::fflush(fp); /* CHECKPOINT 1 */

#if defined(__x86_64__) || defined(_M_X64)
    {
        void* caller = SafeReadPointer(regs.sp);
        if (caller) {
            std::fprintf(fp, "Caller (*sp):0x%lx\n",
                         static_cast<unsigned long>(reinterpret_cast<uintptr_t>(caller)));
        }
        void* fp_link = regs.bp;
        for (int i = 0; i < 16 && fp_link; i++) {
            void* saved_rbp = SafeReadPointer(fp_link);
            void* saved_ret = SafeReadPointer(static_cast<char*>(fp_link) + 8);
            if (!saved_ret) break;
            std::fprintf(fp, "fp[%d]:       0x%lx\n", i,
                         static_cast<unsigned long>(reinterpret_cast<uintptr_t>(saved_ret)));
            if (saved_rbp == fp_link) break;
            fp_link = saved_rbp;
        }
    }
#elif defined(__aarch64__)
    if (regs.lr) {
        std::fprintf(fp, "Caller (lr): 0x%lx\n",
                     static_cast<unsigned long>(reinterpret_cast<uintptr_t>(regs.lr)));
    }
#endif
    std::fflush(fp); /* CHECKPOINT 2 — raw frame chain durable */

    /* Stage 2 — try to resolve via dladdr to add a binary-base line and
     * symbol names. dladdr is NOT signal-safe and can deadlock or fault
     * if the signal interrupted code that already held the linker lock.
     * If it crashes us we still have the absolute IPs from above. */
    {
        Dl_info self_info{};
        if (dladdr(reinterpret_cast<void*>(&WriteBacktracePosix), &self_info) && self_info.dli_fbase) {
            std::fprintf(fp, "\nBinary base: 0x%lx  (subtract from above for offsets)\n",
                         static_cast<unsigned long>(reinterpret_cast<uintptr_t>(self_info.dli_fbase)));
            std::fprintf(fp, "Resolve:     addr2line -e tmc_pc -fp <offset>\n");
        }
    }
    std::fflush(fp);

    std::fprintf(fp, "\n--- symbolicated (best-effort) ---\n");
    if (regs.ip) WriteResolvedAddr(fp, "Crash IP:    ", regs.ip);
#if defined(__x86_64__) || defined(_M_X64)
    {
        void* caller = SafeReadPointer(regs.sp);
        if (caller) WriteResolvedAddr(fp, "Caller (*sp):", caller);
    }
#endif
    std::fprintf(fp, "\nHandler stack (backtrace() — does not cross the signal frame):\n");
    std::fflush(fp);
    void* frames[64];
    int n = backtrace(frames, 64);
    backtrace_symbols_fd(frames, n, fileno(fp));

    /* Append addr2line file:line lines for the crash IP + the captured
     * frames. addr2line gives the precise source location (and walks
     * inlined call sites with -i) which dladdr can't, turning every
     * crash report into actionable file:LINE pointers without the
     * tester having to install/run the toolchain. Best-effort — if
     * the addr2line binary isn't on PATH (e.g. minimal release tarball)
     * the section is just absent. */
    {
        std::fflush(fp);
        void* probe[64 + 1];
        int pn = 0;
        if (regs.ip) probe[pn++] = regs.ip;
        for (int i = 0; i < n && pn < (int)(sizeof(probe) / sizeof(probe[0])); i++) {
            probe[pn++] = frames[i];
        }
        RunAddr2Line(fileno(fp), probe, pn);
    }

    std::fclose(fp);
}
#endif

#ifdef _WIN32
void WriteBacktraceWindows(const std::filesystem::path& path, CONTEXT* ctx) {
    HANDLE proc = GetCurrentProcess();
    SymInitialize(proc, nullptr, TRUE);

    void* frames[64];
    USHORT n = CaptureStackBackTrace(0, 64, frames, nullptr);

    FILE* fp = std::fopen(path.string().c_str(), "wb");
    if (!fp) {
        return;
    }

    char symBuf[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO* sym = reinterpret_cast<SYMBOL_INFO*>(symBuf);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;

    if (ctx) {
        std::fprintf(fp, "Exception context:\n");
        std::fprintf(fp, "  RIP=0x%llx\n", static_cast<unsigned long long>(ctx->Rip));
    }
    for (USHORT i = 0; i < n; i++) {
        DWORD64 addr = reinterpret_cast<DWORD64>(frames[i]);
        DWORD64 displ = 0;
        if (SymFromAddr(proc, addr, &displ, sym)) {
            std::fprintf(fp, "  [%2u] 0x%llx %s+0x%llx\n", i,
                         static_cast<unsigned long long>(addr),
                         sym->Name,
                         static_cast<unsigned long long>(displ));
        } else {
            std::fprintf(fp, "  [%2u] 0x%llx (no symbol)\n", i,
                         static_cast<unsigned long long>(addr));
        }
    }
    std::fclose(fp);
}
#endif

/* Re-entry guard: a fault while we're already capturing should not loop.
 * 0 = idle, 1 = capturing. compare_exchange flips to 1 on entry. */
std::atomic<int> g_capturing{0};

} // namespace

extern "C" char* Port_BugReport_Capture(const char* reason) {
    int expected = 0;
    if (!g_capturing.compare_exchange_strong(expected, 1)) {
        return nullptr;
    }
    struct Releaser {
        ~Releaser() { g_capturing.store(0); }
    } releaser;

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
    ok &= WriteScreenshotPNG(std::filesystem::path(dirname) / "screenshot.png");
    ok &= CopySaveFile(std::filesystem::path(dirname) / "save.bin");
    ok &= WriteStateText(std::filesystem::path(dirname) / "state.txt", s, reason);
#if defined(__linux__)
    /* Snapshot /proc/self/maps too. Best-effort — failure doesn't taint
     * the bundle's `ok` flag because crash reports without maps are
     * still useful (just harder to addr2line). */
    CopyProcMaps(std::filesystem::path(dirname) / "maps.txt");
#endif
    /* Flush libc's stderr buffer into the pipe so the reader thread can
     * append it to the ring before we snapshot. Best-effort: the reader
     * is asynchronous and on the crash path we cannot block (no sleep),
     * so we may miss the last microseconds of output. The dump itself
     * is async-signal-safe (no malloc / no locks / write(2) only). */
    std::fflush(stderr);
#if defined(__linux__) || defined(__APPLE__)
    if (g_stderr_capture_installed.load()) {
        std::string p = (std::filesystem::path(dirname) / "stderr.log").string();
        g_stderrRing.DumpTo(p.c_str());
    }
#endif

    std::fprintf(stderr, "[BUG] Captured %s (ok=%d, reason=%s)\n",
                 dirname.c_str(), ok ? 1 : 0, reason ? reason : "user");

    char* out = static_cast<char*>(std::malloc(dirname.size() + 1));
    if (out) {
        std::memcpy(out, dirname.c_str(), dirname.size() + 1);
    }
    return out;
}

/* ---------- Crash handlers ---------- */

namespace {

#if defined(__linux__) || defined(__APPLE__)

const char* SignalName(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV";
        case SIGABRT: return "SIGABRT";
        case SIGFPE:  return "SIGFPE";
        case SIGILL:  return "SIGILL";
        case SIGBUS:  return "SIGBUS";
        default:      return "SIG?";
    }
}

/* Alternate signal stack so we can still capture on stack-overflow SEGV.
 * 64 KiB — glibc 2.34+ made SIGSTKSZ a runtime sysconf, so we can't size
 * statically off it; this is comfortably above MINSIGSTKSZ on supported
 * targets and small enough not to bloat .bss. */
alignas(16) uint8_t g_altstack[65536];

void CrashHandlerPosix(int sig, siginfo_t* info, void* ucontext) {
    CrashRegs regs = ExtractCrashRegs(ucontext);
    void* fault_addr = info ? info->si_addr : nullptr;

    char reason[96];
    std::snprintf(reason, sizeof(reason), "crash:%s@%p ip=%p",
                  SignalName(sig), fault_addr, regs.ip);

    char* dir = Port_BugReport_Capture(reason);
    if (dir) {
        WriteBacktracePosix(std::filesystem::path(dir) / "backtrace.txt",
                            regs, fault_addr);
        std::free(dir);
    }

    /* SA_RESETHAND was set during installation, so the next delivery uses
     * the default disposition. Re-raise to let the OS produce a core dump
     * and exit with the conventional 128+sig status. */
    std::raise(sig);
}

void InstallPosixHandlers() {
    stack_t ss{};
    ss.ss_sp = g_altstack;
    ss.ss_size = sizeof(g_altstack);
    ss.ss_flags = 0;
    sigaltstack(&ss, nullptr);

    struct sigaction sa {};
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = CrashHandlerPosix;

    int signals[] = { SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS };
    for (int s : signals) {
        sigaction(s, &sa, nullptr);
    }
}

#endif /* POSIX */

#ifdef _WIN32

LONG WINAPI CrashHandlerWindows(EXCEPTION_POINTERS* ep) {
    char reason[96];
    DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
    void* addr = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : nullptr;
    std::snprintf(reason, sizeof(reason), "crash:0x%08lx@%p",
                  static_cast<unsigned long>(code), addr);

    char* dir = Port_BugReport_Capture(reason);
    if (dir) {
        WriteBacktraceWindows(std::filesystem::path(dir) / "backtrace.txt",
                              ep ? ep->ContextRecord : nullptr);
        std::free(dir);
    }

    /* Let WER / the debugger pick up after us. */
    return EXCEPTION_CONTINUE_SEARCH;
}

void InstallWindowsHandler() {
    SetUnhandledExceptionFilter(CrashHandlerWindows);
}

#endif /* _WIN32 */

std::atomic<int> g_handlers_installed{0};

} // namespace

extern "C" void Port_BugReport_InstallCrashHandlers(void) {
    int expected = 0;
    if (!g_handlers_installed.compare_exchange_strong(expected, 1)) {
        return;
    }
    /* Capture stderr first so any messages emitted by the crash handler
     * itself land in stderr.log too. */
    InstallStderrCapture();
#if defined(__linux__) || defined(__APPLE__)
    InstallPosixHandlers();
#endif
#ifdef _WIN32
    InstallWindowsHandler();
#endif
}
