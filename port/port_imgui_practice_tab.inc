/* Practice ribbon tab — extracted from port_imgui_menu.cpp. Included into the same
 * translation unit; relies on the declarations already visible there. */
/* Defined alongside DrawPracticeOverlay below; used here in the ribbon tab. */
static void Practice_FormatFrames(unsigned long long frames, char* out, size_t cap);

static void DrawRibbonPracticeTab(void) {
    ImGui::TextWrapped("Speedrun practice tools. Overlays draw over the game "
                       "whenever their toggle is on (independent of this menu).");
    ImGui::Separator();

    ImGui::SeparatorText("Overlays");
    bool t = Port_Config_GetPracticeShowTimer();
    if (ImGui::Checkbox("Show IGT timer", &t))
        Port_Config_SetPracticeShowTimer(t);
    bool in = Port_Config_GetPracticeShowInputs();
    if (ImGui::Checkbox("Show input display", &in))
        Port_Config_SetPracticeShowInputs(in);
    bool h = Port_Config_GetPracticeShowHistory();
    if (ImGui::Checkbox("Show input history", &h))
        Port_Config_SetPracticeShowHistory(h);

    ImGui::SeparatorText("Timer");
    char buf[32];
    Practice_FormatFrames(Port_Practice_ElapsedFrames(), buf, sizeof(buf));
    ImGui::Text("Elapsed: %s  (%llu frames)", buf, (unsigned long long)Port_Practice_ElapsedFrames());
    if (ImGui::Button(Port_Practice_TimerRunning() ? "Stop" : "Start"))
        Port_Practice_TimerToggle();
    ImGui::SameLine();
    if (ImGui::Button("Reset timer"))
        Port_Practice_TimerReset();
    ImGui::SameLine();
    if (ImGui::Button("Split"))
        Port_Practice_AddSplit();

    int nsplits = Port_Practice_SplitCount();
    if (nsplits > 0) {
        ImGui::SameLine();
        if (ImGui::Button("Clear splits"))
            Port_Practice_ClearSplits();
        if (ImGui::BeginTable("##splits", 3,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("#");
            ImGui::TableSetupColumn("Time");
            ImGui::TableSetupColumn("Delta");
            ImGui::TableHeadersRow();
            unsigned long long prev = 0;
            for (int i = 0; i < nsplits; ++i) {
                unsigned long long f = Port_Practice_SplitAt(i);
                char tbuf[32], dbuf[32];
                Practice_FormatFrames(f, tbuf, sizeof(tbuf));
                Practice_FormatFrames(f - prev, dbuf, sizeof(dbuf));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%d", i + 1);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(tbuf);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("+%s", dbuf);
                prev = f;
            }
            ImGui::EndTable();
        }
    }

    ImGui::SeparatorText("Practice point");
    if (ImGui::Button("Set point")) {
        if (Port_Practice_SetPoint())
            Port_DebugMenu_ToastFromExternal("Practice point set");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!Port_Practice_HasPoint());
    if (ImGui::Button("Reload point")) {
        Port_DebugMenu_ToastFromExternal(Port_Practice_LoadPoint() ? "Practice point loaded" : "Reload failed");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled(Port_Practice_HasPoint() ? "(set)" : "(empty)");

    ImGui::SeparatorText("Speed");
    float sm = Port_Config_GetPracticeSlowmo();
    if (ImGui::SliderFloat("Slow-mo", &sm, 0.1f, 1.0f, "%.2fx")) {
        Port_Config_SetPracticeSlowmo(sm);
    }
    ImGui::SameLine();
    if (ImGui::Button("1x"))
        Port_Config_SetPracticeSlowmo(1.0f);
    if (ImGui::Button(Port_Practice_IsPaused() ? "Resume" : "Pause"))
        Port_Practice_TogglePause();

    ImGui::SeparatorText("Hotkeys");
    ImGui::TextDisabled("Keyboard:  [ set point   ] reload   P pause   . frame-advance   ' reset   ; split\n"
                        "Gamepad:   hold Select + A reload / B set / X pause / Y advance / D-Up reset / D-Down split");
}
