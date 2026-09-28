/* Accessibility ribbon tab — extracted from port_imgui_menu.cpp. Included into the same
 * translation unit; relies on the declarations already visible there. */
static void DrawRibbonAccessibilityTab(void) {
    Port_TTS_Init(); /* idempotent — safe if main.c already initialised */
    const char* backendName = Port_TTS_GetBackendName();

    ImGui::TextWrapped("Text-to-speech reads important UI labels aloud (focused "
                       "buttons, dialogs, errors). Toggle off at any time. Default "
                       "off; settings persist across launches.");
    ImGui::Separator();

    if (ImGui::BeginTable("##tts_table", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch);

        // Backend row
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Backend");
        ImGui::TableSetColumnIndex(1);
        if (backendName) {
            ImGui::TextUnformatted(backendName);
            if (std::strcmp(backendName, "NVDA") == 0) {
                ImGui::SameLine();
                ImGui::TextDisabled("(rate/pitch/volume ignored - NVDA controls those)");
            }
        } else {
            ImGui::TextDisabled("(unavailable - install spd-say / espeak-ng on Linux)");
        }

        // Enable Row
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Enable TTS");
        ImGui::TableSetColumnIndex(1);
        bool on = Port_TTS_GetEnabled();
        if (ImGui::Checkbox("##enable_tts", &on)) {
            Port_TTS_SetEnabled(on);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(F7 toggles, F6 stops speech)");

        // Rate
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Rate");
        ImGui::TableSetColumnIndex(1);
        float rate = Port_TTS_GetRate();
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::SliderFloat("##rate", &rate, 0.0f, 1.0f, "%.2f")) {
            Port_TTS_SetRate(rate);
        }

        // Pitch
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Pitch");
        ImGui::TableSetColumnIndex(1);
        float pitch = Port_TTS_GetPitch();
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::SliderFloat("##pitch", &pitch, 0.0f, 1.0f, "%.2f")) {
            Port_TTS_SetPitch(pitch);
        }

        // Volume
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Volume");
        ImGui::TableSetColumnIndex(1);
        float volume = Port_TTS_GetVolume();
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::SliderFloat("##volume", &volume, 0.0f, 1.0f, "%.2f")) {
            Port_TTS_SetVolume(volume);
        }

        // Voice
        static char voiceBuf[128];
        static char langBuf[32];
        static bool inited = false;
        if (!inited) {
            const char* v = Port_TTS_GetVoice();
            const char* l = Port_TTS_GetLanguage();
            std::strncpy(voiceBuf, v ? v : "", sizeof(voiceBuf) - 1);
            std::strncpy(langBuf, l ? l : "", sizeof(langBuf) - 1);
            inited = true;
        }
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Voice");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::InputText("##voice", voiceBuf, sizeof(voiceBuf))) {
            Port_TTS_SetVoice(voiceBuf);
        }

        // Language
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Language");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::InputText("##lang", langBuf, sizeof(langBuf))) {
            Port_TTS_SetLanguage(langBuf);
        }

        ImGui::EndTable();
    }
    ImGui::TextDisabled("Voice IDs vary by backend (espeak: 'en+f2', say: 'Samantha', SAPI: 'Microsoft David').");

    ImGui::Separator();
    if (ImGui::Button("Test voice")) {
        PortTtsOptions o = {};
        o.rate = o.pitch = o.volume = 0.0f / 0.0f;
        o.dedupe = false;
        Port_TTS_Speak("This is the Project Picori text-to-speech test. "
                       "If you hear this, T T S is wired up.",
                       &o);
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
        Port_TTS_Stop();
    }
    ImGui::SameLine();
    if (ImGui::Button("Read focus")) {
        Port_TTS_Speak("Focus reader test. The focused control reads aloud as you Tab.", nullptr);
    }

    ImGui::Separator();
    ImGui::TextWrapped("Navigation cues (for blind / low-vision players). On-demand keys "
                       "in game: F10 scans nearby points of interest (chests, items, NPCs, "
                       "animals, enemies, exits); Shift+F10 steps through them one at a "
                       "time; Ctrl+F10 reads the surface under you, walls around you, and "
                       "exits.");
    if (ImGui::Button("Scan surroundings (F10)")) {
        Port_A11y_ScanSurroundings();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cycle (Shift+F10)")) {
        Port_A11y_CycleNext();
    }
    ImGui::SameLine();
    if (ImGui::Button("Look around (Ctrl+F10)")) {
        Port_A11y_LookAround();
    }

    ImGui::Spacing();
    ImGui::TextWrapped("Passive cues play automatically as you move: a tonal enemy radar "
                       "(stereo pan = direction, pitch = distance), footstep sounds tinted "
                       "by surface, fall-hazard warnings, and wall bumps.");
    {
        bool b;
        b = Port_A11y_GetPassiveEnabled();
        if (ImGui::Checkbox("Passive cues", &b))
            Port_A11y_SetPassiveEnabled(b);
        b = Port_A11y_GetFootstepsEnabled();
        if (ImGui::Checkbox("Footsteps", &b))
            Port_A11y_SetFootstepsEnabled(b);
        ImGui::SameLine();
        b = Port_A11y_GetHazardsEnabled();
        if (ImGui::Checkbox("Hazards", &b))
            Port_A11y_SetHazardsEnabled(b);
        ImGui::SameLine();
        b = Port_A11y_GetRadarEnabled();
        if (ImGui::Checkbox("Enemy radar", &b))
            Port_A11y_SetRadarEnabled(b);
        ImGui::SameLine();
        b = Port_A11y_GetWallsEnabled();
        if (ImGui::Checkbox("Walls", &b))
            Port_A11y_SetWallsEnabled(b);
    }

    ImGui::Separator();
    ImGui::TextWrapped("Manual test plan:\n"
                       "  1. Enable above, click Test voice - hear the test line.\n"
                       "  2. Tab through this tab's controls - each label announces.\n"
                       "  3. F7 toggles TTS without opening the menu.\n"
                       "  4. F6 stops mid-utterance.\n"
                       "  5. Open a save-overwrite dialog - modal is announced.");
}
