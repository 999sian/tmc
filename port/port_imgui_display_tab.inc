/* DrawRibbonDisplayTab — extracted from port_imgui_menu.cpp so the
 * per-tab UI code stays in smaller, navigable chunks. This file is
 * included into the same translation unit and intentionally relies on
 * the surrounding ImGui/Port_* declarations already visible there. */
static void DrawRibbonDisplayTab(void) {
    const bool gpuActive = Port_GPU_IsActive();

    if (ImGui::BeginTable("##display_settings_table", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

        // Window scale
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::Text("Window scale");
        ImGui::TableSetColumnIndex(1);
        int scale = (int)Port_PPU_WindowScale();
        if (ImGui::Button("<##scale")) Port_PPU_CycleWindowScale(-1);
        ImGui::SameLine(); ImGui::Text("%dx", scale); ImGui::SameLine();
        if (ImGui::Button(">##scale")) Port_PPU_CycleWindowScale(+1);

        // Filter
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::Text("Filter");
        ImGui::TableSetColumnIndex(1);
        if (ImGui::Button("<##filter")) Port_PPU_CyclePresentationMode(-1);
        ImGui::SameLine(); ImGui::Text("%s", Port_PPU_PresentationModeName()); ImGui::SameLine();
        if (ImGui::Button(">##filter")) Port_PPU_CyclePresentationMode(+1);

        // Target FPS
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::Text("Target FPS");
        ImGui::TableSetColumnIndex(1);
        if (ImGui::Button("<##fps")) Port_Config_CycleTargetFps(-1);
        ImGui::SameLine();
        unsigned fps = Port_Config_TargetFps();
        if (fps == 0) ImGui::Text("uncapped");
        else          ImGui::Text("%u", fps);
        ImGui::SameLine();
        if (ImGui::Button(">##fps")) Port_Config_CycleTargetFps(+1);

        // Internal scale
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::Text("Internal scale");
        ImGui::TableSetColumnIndex(1);
        if (ImGui::Button("<##iscale")) Port_Config_CycleInternalScale(-1);
        ImGui::SameLine(); ImGui::Text("%ux", (unsigned)Port_Config_InternalScale()); ImGui::SameLine();
        if (ImGui::Button(">##iscale")) Port_Config_CycleInternalScale(+1);
        RandoUi_HelpTooltip(
            "Renders the GBA frame at this integer multiple internally before "
            "the final upscale. It only changes sampling quality when a shader "
            "or the widescreen path resamples the image - with the default "
            "nearest-neighbour 2D output it has no visible effect. Leave at 1x "
            "unless a shader you're using benefits from supersampling.");

        // Aspect ratio
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::Text("Aspect ratio");
        ImGui::TableSetColumnIndex(1);
        if (ImGui::Button("<##aspect")) Port_Config_CycleAspectMode(-1);
        ImGui::SameLine(); ImGui::Text("%s", Port_Config_AspectModeName(Port_Config_AspectMode())); ImGui::SameLine();
        if (ImGui::Button(">##aspect")) Port_Config_CycleAspectMode(+1);

        // Renderer
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::Text("Renderer");
        ImGui::TableSetColumnIndex(1);
        if (ImGui::Button("<##renderer")) Port_Config_CycleRenderBackend(-1);
        ImGui::SameLine(); ImGui::Text("%s", Port_Config_RenderBackendName(Port_Config_RenderBackend())); ImGui::SameLine();
        if (ImGui::Button(">##renderer")) Port_Config_CycleRenderBackend(+1);
        ImGui::SameLine(); ImGui::TextDisabled("(restart)");

        // Background
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::Text("Background");
        ImGui::TableSetColumnIndex(1);
        if (ImGui::Button("<##bgfill")) Port_Config_CycleBgFill(-1);
        ImGui::SameLine(); ImGui::Text("%s", Port_Config_BgFillName(Port_Config_BgFill())); ImGui::SameLine();
        if (ImGui::Button(">##bgfill")) Port_Config_CycleBgFill(+1);

        // CRT filter
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::Text("CRT filter");
        ImGui::TableSetColumnIndex(1);
        if (ImGui::Button("<##crt")) Port_PPU_CycleFilter(-1);
        ImGui::SameLine(); ImGui::Text("%s", Port_PPU_FilterName()); ImGui::SameLine();
        if (ImGui::Button(">##crt")) Port_PPU_CycleFilter(+1);

        ImGui::EndTable();
    }

    if (Port_Config_BgFill() == PORT_BG_FILL_SOLID_COLOR) {
        uint8_t r8 = 0, g8 = 0, b8 = 0;
        Port_Config_BgFillColor(&r8, &g8, &b8);
        int rgb[3] = { (int)r8, (int)g8, (int)b8 };
        bool changed = false;
        ImGui::Indent(150.0f);
        ImGui::PushItemWidth(120.0f);
        if (ImGui::SliderInt("R##bgcol_r", &rgb[0], 0, 255)) {
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::SliderInt("G##bgcol_g", &rgb[1], 0, 255)) {
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::SliderInt("B##bgcol_b", &rgb[2], 0, 255)) {
            changed = true;
        }
        ImGui::PopItemWidth();
        ImGui::SameLine();
        ImVec4 sw = ImVec4(rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, 1.0f);
        ImGui::ColorButton("##bgcol_preview", sw,
                           ImGuiColorEditFlags_NoPicker |
                           ImGuiColorEditFlags_NoTooltip,
                           ImVec2(22, 22));
        ImGui::Unindent(150.0f);
        if (changed) {
            Port_Config_SetBgFillColor((uint8_t)rgb[0], (uint8_t)rgb[1], (uint8_t)rgb[2]);
        }
    }

    const bool glslpSupported = gpuActive && Port_GPU_SupportsGlslpRuntime();
    if (!glslpSupported) {
        ImGui::Indent(150.0f);
        ImGui::TextDisabled("Shader preset: %s", gpuActive ? "(SPIR-V backend required)" : "(GPU backend required)");
        ImGui::Unindent(150.0f);
    } else {
        static std::vector<std::string> sPresetPaths;
        static std::string              sActivePreset;
        static bool                     sScannedOnce = false;

        auto refresh = [&] {
            sPresetPaths.clear();
            std::error_code ec;
            std::vector<std::string> roots;
            if (const char* base = SDL_GetBasePath()) {
                std::string b = base;
                roots.push_back(b + "shaders");
                roots.push_back(b + "glslp");
                roots.push_back(b);
            }
            roots.emplace_back("shaders");
            roots.emplace_back("glslp");
            roots.emplace_back(".");
            roots.emplace_back("/tmp/glslp_demo");
            for (const auto& root : roots) {
                std::error_code dec;
                if (!std::filesystem::is_directory(root, dec)) continue;
                /* skip_permission_denied + the non-throwing increment(ec)
                 * overload: the range-for/`operator++` form throws
                 * filesystem_error mid-recursion on an unreadable entry,
                 * and nothing here catches it -> std::terminate. */
                std::filesystem::recursive_directory_iterator it(
                    root, std::filesystem::directory_options::skip_permission_denied, dec);
                const std::filesystem::recursive_directory_iterator end;
                for (; !dec && it != end; it.increment(dec)) {
                    std::error_code fec;
                    if (it->is_regular_file(fec) && it->path().extension() == ".glslp") {
                        sPresetPaths.push_back(it->path().string());
                    }
                }
            }
            std::sort(sPresetPaths.begin(), sPresetPaths.end());
            sPresetPaths.erase(std::unique(sPresetPaths.begin(), sPresetPaths.end()),
                               sPresetPaths.end());
        };
        if (!sScannedOnce) {
            refresh();
            /* Reflect the preset loaded from config at startup (#146) so the
             * list shows it selected and "active: ..." renders. */
            if (sActivePreset.empty() && Port_GlslpRuntime_IsActive()) {
                const char* sp = Port_Config_GetShaderPreset();
                if (sp && sp[0]) sActivePreset = sp;
            }
            sScannedOnce = true;
        }

        ImGui::Indent(150.0f);
        ImGui::TextUnformatted("Shader preset"); ImGui::SameLine();
        if (ImGui::Button("Off##preset")) {
            Port_GlslpRuntime_Unload();
            sActivePreset.clear();
            Port_Config_SetShaderPreset("");                  /* persist (#146) */
            Port_DebugMenu_ToastFromExternal("Shader preset off");
        }
        ImGui::SameLine();
        if (ImGui::Button("Rescan##preset")) refresh();
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu found)", sPresetPaths.size());

        const bool active = Port_GlslpRuntime_IsActive() != 0;
        if (active && !sActivePreset.empty()) {
            ImGui::TextDisabled("  active: %s", sActivePreset.c_str());
        }

        if (!sPresetPaths.empty()) {
            if (ImGui::BeginListBox("##preset_list",
                    ImVec2(ImGui::GetContentRegionAvail().x - 4.0f, ImGui::GetTextLineHeightWithSpacing() * 3.5f))) {
                for (size_t i = 0; i < sPresetPaths.size(); ++i) {
                    bool selected = (sPresetPaths[i] == sActivePreset)
                                    && Port_GlslpRuntime_IsActive();
                    std::string label = std::filesystem::path(sPresetPaths[i]).filename().string();
                    label += "    "; label += sPresetPaths[i];
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        if (selected) {
                            Port_GlslpRuntime_Unload();
                            sActivePreset.clear();
                            Port_Config_SetShaderPreset("");          /* persist (#146) */
                            Port_DebugMenu_ToastFromExternal("Shader preset off");
                        } else if (Port_GlslpRuntime_Load(sPresetPaths[i].c_str())) {
                            sActivePreset = sPresetPaths[i];
                            Port_Config_SetShaderPreset(sActivePreset.c_str()); /* persist (#146) */
                            Port_DebugMenu_ToastFromExternal("Preset loaded");
                        } else {
                            Port_DebugMenu_ToastFromExternal("Preset load FAILED");
                        }
                    }
                }
                ImGui::EndListBox();
            }
        }
        ImGui::Unindent(150.0f);
    }

    ImGui::Separator();

#if defined(MODE1_GBA_WIDTH) && (MODE1_GBA_WIDTH > 240)
    {
        bool ws = Port_Config_WidescreenEnabled();
        if (ImGui::Checkbox("Widescreen (WIP)", &ws)) {
            Port_Config_SetWidescreenEnabled(ws);
            Port_PPU_ApplyWindowScale();
            Port_DebugMenu_ToastFromExternal(ws ? "Widescreen enabled" : "Widescreen disabled");
        }
        ImGui::SameLine();
    }
#endif

    bool fs = Port_PPU_IsFullscreen();
    if (ImGui::Checkbox("Fullscreen", &fs)) {
        Port_PPU_ToggleFullscreen();
        Port_Config_SetFullscreen(Port_PPU_IsFullscreen());   /* persist (#146) */
    }
    ImGui::SameLine();

    bool vsync = Port_PPU_VSyncEnabled();
    if (ImGui::Checkbox("VSync", &vsync)) {
        Port_PPU_SetVSync(vsync);
        Port_Config_SetVSync(vsync);                          /* persist (#146) */
    }
    ImGui::SameLine();

    bool drp = Port_DiscordRpc_IsEnabled();
    if (ImGui::Checkbox("Discord RPC", &drp)) {
        Port_DiscordRpc_SetEnabled(drp);
        Port_Config_SetDiscordRpc(drp);                       /* persist (#146) */
    }
    RandoUi_HelpTooltip(
        "Publishes your current area and heart/rupee count to Discord as a "
        "Rich Presence status.\n\n"
        "Requires a Discord application ID compiled into the build "
        "(discord_app_id.txt, not shipped by default), so it stays inactive in "
        "release builds that ship without one. Also currently a no-op on "
        "Windows (the named-pipe IPC client is not yet implemented).");

    bool colourCorrect = Port_PPU_ColorCorrectionEnabled();
    if (ImGui::Checkbox("GBA colour correction", &colourCorrect)) {
        Port_PPU_SetColorCorrection(colourCorrect);           /* live */
        Port_Config_SetColorCorrection(colourCorrect);        /* persist */
    }
    RandoUi_HelpTooltip(
        "Remaps the raw 15-bit palette through the GBA's dim, high-gamma "
        "reflective-LCD response so colours match the handheld instead of "
        "looking over-bright on a modern sRGB display. Default on.");

    bool lcdPersist = Port_Config_GetLcdPersistence();
    if (ImGui::Checkbox("LCD persistence", &lcdPersist)) {
        Port_Config_SetLcdPersistence(lcdPersist);            /* persist */
        Port_PPU_SetPersistence(lcdPersist, Port_Config_GetLcdPersistenceRho());
    }
    RandoUi_HelpTooltip(
        "Reproduces the slow pixel response (ghosting) of the GBA LCD by "
        "blending each frame into the previous one. Some flicker/dither "
        "transparency effects were designed around it. Default off.");
    if (lcdPersist) {
        float rho = Port_Config_GetLcdPersistenceRho();
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::SliderFloat("Persistence strength", &rho, 0.0f, 0.95f, "%.2f")) {
            Port_Config_SetLcdPersistenceRho(rho);            /* persist */
            Port_PPU_SetPersistence(true, rho);               /* live */
        }
    }

    bool holdAdv = Port_Config_GetHoldToAdvanceText();
    if (ImGui::Checkbox("Hold to advance text", &holdAdv)) {
        Port_Config_SetHoldToAdvanceText(holdAdv);
    }
    RandoUi_HelpTooltip(
        "When on, holding an advance button (A / B / R / D-pad) keeps text "
        "pages turning instead of needing a fresh press each page. Default off "
        "preserves GBA-accurate timing for timed cutscenes.");
}
