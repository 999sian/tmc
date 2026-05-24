/* port/vk_rt_experiment/FrameSource.h
 *
 * Stand-in for the live GBA framebuffer (`virtuappu_frame_buffer` in
 * tmc_pc). Loads a directory of pre-captured 240×160 RGBA PNG
 * screenshots at startup, then `currentFrame()` returns the bytes of
 * one frame per call — advancing through the loaded set at a tunable
 * rate so the RT pipeline sees a changing-each-frame texture, the
 * same shape it would see if hooked to the real PPU.
 *
 * Why screenshots and not a live IPC connection? Two reasons:
 *   1. The RT scaffold is a separate binary from tmc_pc — linking
 *      them at this stage means changing the main game's build,
 *      which is bigger than the "prove the bridge" goal.
 *   2. The captured PNGs let us iterate on the RT side without
 *      having to run a full TMC session each time.
 *
 * The actual GBA-frame-to-RT path is identical: `uint8_t*` of size
 * 240*160*4, uploaded via staging buffer + image-copy command. Once
 * this is proven, swapping screenshots for a shared-memory feed from
 * tmc_pc is a few-dozen-line change.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tmc_vkrt {

class FrameSource {
public:
    /* Width/height of every frame in the source set. Matches the
     * GBA's native framebuffer dimensions exactly so the upload sizing
     * doesn't depend on the source. */
    static constexpr int kFrameW = 240;
    static constexpr int kFrameH = 160;
    static constexpr size_t kFrameBytes =
        static_cast<size_t>(kFrameW) * kFrameH * 4;

    /* Load every `tmc-*.png` in `dir` whose dimensions are kFrameW ×
     * kFrameH. Returns true if at least one was loaded. Frames are
     * stored in lexicographic name order — so tmc-0.png is at index
     * 0, tmc-1.png at 1, ... tmc-17.png at 17. */
    bool loadDirectory(const std::string& dir);

    /* Number of distinct frames loaded. 0 if loadDirectory failed. */
    size_t frameCount() const { return mFrames.size(); }

    /* Advance the internal frame index and return the new current
     * frame's RGBA bytes. `framesPerStep` lets the caller throttle:
     * e.g. RT runs at 60fps but we only advance the PPU frame every
     * 10 calls (= ~6fps source rate). Returns nullptr if no frames
     * were loaded. The pointer is valid until the next call. */
    const uint8_t* advance(int framesPerStep);

    /* Read-only access to the current frame without advancing. */
    const uint8_t* currentFrame() const {
        if (mFrames.empty()) return nullptr;
        return mFrames[mCurrent].data();
    }

private:
    /* Decode one PNG file from `path` into a kFrameW × kFrameH RGBA
     * buffer. Returns empty vector if the file isn't a PNG, isn't
     * the expected size, or fails to load. */
    std::vector<uint8_t> decodePng(const std::string& path);

    std::vector<std::vector<uint8_t>> mFrames;
    size_t mCurrent = 0;
    int    mCallsSinceStep = 0;
};

}  /* namespace tmc_vkrt */
