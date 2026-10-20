#include "port_hd_text.h"
#include "port_hd_text_geometry.h"
#include "imgui.h"

extern "C" bool Message_CopyTextCanvas(uint8_t* pixels, int capacity, int* x, int* y, int* width, int* height);

extern "C" void Port_HDText_Draw(const uint32_t* frame, int frameWidth, int frameHeight, int pitchPixels,
                                  float viewportX, float viewportY, float viewportWidth, float viewportHeight) {
    if (!ImGui::GetCurrentContext() || viewportWidth <= 0 || viewportHeight <= 0 || frameWidth < 240 || frameHeight != 160)
        return;
    uint8_t pixels[PortHDText::kMaxPixels];
    int x, y, width, height;
    if (!Message_CopyTextCanvas(pixels, sizeof(pixels), &x, &y, &width, &height))
        return;
    x += (frameWidth - 240) / 2;
    uint32_t colors[16] = {};
    if (!PortHDText::MatchCanvas(pixels, width, height, frame, frameWidth, frameHeight, pitchPixels, x, y, colors))
        return;
    const float sx = viewportWidth / frameWidth, sy = viewportHeight / frameHeight;
    if (sx < 2.0f || sy < 2.0f)
        return;
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    const ImVec2 origin(viewportX + x * sx, viewportY + y * sy);
    draw->PushClipRect(origin, ImVec2(origin.x + width * sx, origin.y + height * sy), true);
    draw->AddRectFilled(origin, ImVec2(origin.x + width * sx, origin.y + height * sy), colors[15]);
    // shortcut: ROM contours have no extra font detail, use licensed outline glyphs when more detail is needed.
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            const unsigned index = pixels[row * width + col];
            if (index == 15)
                continue;
            const float tl = PortHDText::CornerInset(pixels, width, height, col, row, -1, -1);
            const float tr = PortHDText::CornerInset(pixels, width, height, col, row, 1, -1);
            const float br = PortHDText::CornerInset(pixels, width, height, col, row, 1, 1);
            const float bl = PortHDText::CornerInset(pixels, width, height, col, row, -1, 1);
            const float left = origin.x + col * sx, top = origin.y + row * sy;
            const auto point = [&](float px, float py) { return ImVec2(left + px * sx, top + py * sy); };
            const ImVec2 corners[] = {point(tl, 0), point(1-tr, 0), point(1, tr), point(1, 1-br),
                                      point(1-br, 1), point(bl, 1), point(0, 1-bl), point(0, tl)};
            ImVec2 contour[8];
            int count = 0;
            for (const ImVec2& p : corners) {
                if (count == 0 || p.x != contour[count - 1].x || p.y != contour[count - 1].y)
                    contour[count++] = p;
            }
            if (count > 1 && contour[0].x == contour[count - 1].x && contour[0].y == contour[count - 1].y)
                --count;
            // Interior cells share opaque edges. Feather only the exterior,
            // otherwise antialiasing each cell creates seams inside letters.
            const ImDrawListFlags flags = draw->Flags;
            draw->Flags &= ~ImDrawListFlags_AntiAliasedFill;
            draw->AddConvexPolyFilled(contour, count, colors[index]);
            draw->Flags = flags;
            const auto edge = [&](int a, int b, bool exposed) {
                if (exposed && (corners[a].x != corners[b].x || corners[a].y != corners[b].y))
                    draw->AddLine(corners[a], corners[b], colors[index], 1.0f);
            };
            edge(0, 1, PortHDText::Background(pixels, width, height, col, row - 1));
            edge(1, 2, tr > 0);
            edge(2, 3, PortHDText::Background(pixels, width, height, col + 1, row));
            edge(3, 4, br > 0);
            edge(4, 5, PortHDText::Background(pixels, width, height, col, row + 1));
            edge(5, 6, bl > 0);
            edge(6, 7, PortHDText::Background(pixels, width, height, col - 1, row));
            edge(7, 0, tl > 0);
        }
    }
    draw->PopClipRect();
}
