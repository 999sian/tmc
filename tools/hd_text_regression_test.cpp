// g++ -std=c++17 tools/hd_text_regression_test.cpp -o /tmp/hd_text_test && /tmp/hd_text_test
#include "../port/port_hd_text_geometry.h"
#include <cassert>
#include <cstdio>

int main() {
    using namespace PortHDText;
    const uint8_t pixels[] = {15, 1, 15, 1};
    uint32_t frame[] = {0xff002244, 0xff88aaff, 0xff002244, 0xff88aaff};
    uint32_t colors[16] = {};
    assert(MatchCanvas(pixels, 2, 2, frame, 2, 2, 2, 0, 0, colors));
    assert(colors[1] == 0xff88aaff); // ABGR colors remain unchanged, including fades.
    frame[3] = 0xff123456; // A sprite/effect obscures one glyph: leave original frame.
    assert(!MatchCanvas(pixels, 2, 2, frame, 2, 2, 2, 0, 0, colors));
    assert(!MatchCanvas(pixels, 2, 2, frame, 2, 2, 2, 1, 0, colors));
    assert(!MatchCanvas(pixels, 2, 2, frame, 2, 2, 1, 0, 0, colors));
    const uint8_t transparent[] = {0};
    assert(!MatchCanvas(transparent, 1, 1, frame, 2, 2, 2, 0, 0, colors));
    const uint8_t solid[] = {1, 1, 1, 1};
    assert(CornerInset(solid, 2, 2, 0, 0, -1, -1) == 0.25f);
    assert(CornerInset(solid, 2, 2, 0, 0, 1, 1) == 0.0f);
    assert(CornerInset(solid, 2, 2, 0, 0, -1, 1) == 0.0f);
    const uint8_t coloredNeighbor[] = {1, 2, 15, 15};
    assert(CornerInset(coloredNeighbor, 2, 2, 0, 0, 1, -1) == 0.0f);
    const uint8_t diagonal[] = {1, 15, 15, 1};
    assert(CornerInset(diagonal, 2, 2, 0, 0, 1, 1) == 0.0f); // Keep diagonal strokes connected.
    uint8_t crowded[kMaxPixels];
    uint32_t crowdedFrame[kMaxPixels];
    for (int i = 0; i < kMaxPixels; ++i) { crowded[i] = 1; crowdedFrame[i] = 0xffabcdef; }
    assert(!MatchCanvas(crowded, 208, 32, crowdedFrame, 208, 32, 208, 0, 0, colors));
    std::puts("HD dialogue geometry: passed");
}
