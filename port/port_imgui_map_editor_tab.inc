/* Map Editor ribbon tab — extracted from port_imgui_menu.cpp. Included into the same
 * translation unit; relies on the declarations already visible there. */
extern "C" bool Port_LevelEditor_IsOpen(void);
extern "C" void Port_LevelEditor_Toggle(void);
extern "C" void Port_LevelEditor_Render(void* renderer, int winW, int winH);

static void DrawRibbonMapEditorTab(void) {
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "Direct Level Editor Mode");
    ImGui::Separator();
    bool editorOpen = Port_LevelEditor_IsOpen();
#ifdef TMC_GPU_RENDERER
    /* The overlay draws with SDL_Renderer primitives; there is nothing to
     * draw it with on the SDL_GPU path, so painting would be blind. */
    const bool noOverlay = (sRenderer == nullptr);
#else
    const bool noOverlay = false;
#endif
    ImGui::BeginDisabled(noOverlay);
    if (ImGui::Checkbox("Enable Direct Painting Overlay", &editorOpen)) {
        Port_LevelEditor_Toggle();
        if (editorOpen) {
            extern void Port_DebugMenu_Toggle(void);
            Port_DebugMenu_Toggle();
        }
    }
    ImGui::EndDisabled();
    if (noOverlay)
        ImGui::TextDisabled("Unavailable on the SDL_GPU renderer backend.");
    ImGui::Separator();
    ImGui::TextDisabled("Controls & Hotkeys:");
    ImGui::BulletText("Left-Click   : Paint selected tile");
    ImGui::BulletText("Right-Click  : Eyedropper (sample tile)");
    ImGui::BulletText("[ / ]        : Previous/next tile ID (Shift: step 16)");
    ImGui::BulletText("L            : Switch between Top/Bottom layers");
    ImGui::BulletText("M / F        : Next BGM / fight BGM (Shift: previous)");
    ImGui::BulletText("K            : Room lighting up (Shift: down)");
    ImGui::BulletText("S            : Save room to edited_levels/");
    ImGui::BulletText("Esc          : Close Level Editor");
}
