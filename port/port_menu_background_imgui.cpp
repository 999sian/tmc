#include "port_menu_background.h"
#include "port_ppu.h"

#include <imgui.h>
#include <cmath>
#include <cstring>

extern "C" void Port_MenuBackground_Render(float stageX, float stageY, float stageW, float stageH,
                                           float frameX, float frameY, float frameW, float frameH) {
    if (frameW <= 0 || frameH <= 0 || stageW <= 0 || stageH <= 0 || !ImGui::GetCurrentContext())
        return;
    const bool left = frameX > stageX;
    const bool right = frameX + frameW < stageX + stageW;
    if (!left && !right)
        return;
    const uint32_t* source = Port_MenuBackground_GetPixels();
    if (!source)
        return;
    static uint32_t pixels[256 * 160];
    std::memcpy(pixels, source, sizeof(pixels));
    Port_PPU_ColorCorrectBuffer(pixels, 256 * 160);

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    const float scaleX = frameW / 240.0f;
    const float scaleY = frameH / 160.0f;
    const int first = (int)std::floor((stageX - frameX) / scaleX);
    const int last = (int)std::ceil((stageX + stageW - frameX) / scaleX);
    const auto column = [](int value) { return (value % 240 + 240) % 240; };
    // shortcut: repeat the visible BG3 art; hidden screenblock columns may contain padding.
    for (int side = 0; side < 2; ++side) {
        if ((side == 0 && !left) || (side == 1 && !right))
            continue;
        const int start = side == 0 ? first : 240;
        const int end = side == 0 ? 0 : last;
        draw->PushClipRect(ImVec2(side == 0 ? stageX : frameX + frameW, stageY),
                           ImVec2(side == 0 ? frameX : stageX + stageW, stageY + stageH), true);
        for (int y = 0; y < 160; ++y) {
            for (int x = start; x < end;) {
                const uint32_t color = pixels[y * 256 + column(x)];
                int next = x + 1;
                while (next < end && pixels[y * 256 + column(next)] == color)
                    ++next;
                draw->AddRectFilled(ImVec2(frameX + x * scaleX, frameY + y * scaleY),
                                    ImVec2(frameX + next * scaleX, frameY + (y + 1) * scaleY), color);
                x = next;
            }
        }
        draw->PopClipRect();
    }
}
