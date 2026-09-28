/* Saves ribbon tab — extracted from port_imgui_menu.cpp. Included into the same
 * translation unit; relies on the declarations already visible there. */
static void DrawRibbonSavesTab(void) {
    /* Quit-to-title actions at the top of the tab — high-visibility
     * because the existing pause menu doesn't expose them. */
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.55f, 0.30f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.70f, 0.40f, 1.0f));
    if (ImGui::Button("Save & Quit to Title"))
        DoQuitToTitle(true);
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.35f, 0.30f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.45f, 0.40f, 1.0f));
    if (ImGui::Button("Quit to Title (no save)"))
        DoQuitToTitle(false);
    ImGui::PopStyleColor(2);
    ImGui::Separator();

    /* Console-Parity — run-integrity master switch. Lives here because it
     * makes the save-state controls below inert. */
    {
        bool parity = Port_Config_GetConsoleParity();
        if (ImGui::Checkbox("Console-Parity mode (legit-run integrity)", &parity)) {
            Port_Config_SetConsoleParity(parity);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(360.0f);
            ImGui::TextUnformatted("Holds the port provably equivalent to GBA "
                                   "hardware for legitimate speedruns:\n"
                                   "  - input edge-cache off (1-frame granularity)\n"
                                   "  - save-states inert (no mid-run restores)\n"
                                   "  - widescreen forced off (no early off-screen "
                                   "AI / RNG advance)\n"
                                   "  - frame pacing locked to 59.7275 Hz\n"
                                   "Leave OFF for practice/casual play.");
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        if (parity) {
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f), "Save-states disabled while Console-Parity is ON.");
        }
    }
    ImGui::Separator();

    /* Auto-save controls at the top. */
    bool autoOn = Port_QuickSave_AutoEnabled();
    if (ImGui::Checkbox("Auto-save", &autoOn)) {
        Port_QuickSave_SetAutoEnabled(autoOn ? 1 : 0);
        Port_Config_SetAutosaveEnabled(autoOn);
    }
    ImGui::SameLine(180);
    int sec = (int)(Port_QuickSave_AutoIntervalMs() / 1000u);
    if (ImGui::SliderInt("Interval (s)", &sec, 5, 600)) {
        Port_QuickSave_SetAutoIntervalMs((unsigned)sec * 1000u);
        Port_Config_SetAutosaveIntervalMs((unsigned)sec * 1000u);
    }
    {
        bool areaOn = Port_QuickSave_AutoOnAreaChangeEnabled() != 0;
        if (ImGui::Checkbox("Auto-save on area change", &areaOn)) {
            Port_QuickSave_SetAutoOnAreaChange(areaOn ? 1 : 0);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(360.0f);
            ImGui::TextUnformatted("Fires a snapshot to the auto ring "
                                   "every time you transition between "
                                   "areas/rooms. Independent of the "
                                   "interval timer above.");
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }
    ImGui::Separator();

    /* Slot grid: each slot is one row with Save / Load buttons + timestamp. */
    const int n = Port_QuickSave_SlotCount();
    const int autoBase = Port_QuickSave_AutoSlotBase();
    if (ImGui::BeginTable("##quicksaves_table", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Timestamp", ImGuiTableColumnFlags_WidthStretch);

        for (int s = 0; s < n; ++s) {
            ImGui::PushID(s);
            ImGui::TableNextRow();

            // Column 1: Slot name
            ImGui::TableSetColumnIndex(0);
            const char* tag;
            char tagbuf[16];
            if (s == 0)
                tag = "Quick";
            else if (s < autoBase) {
                std::snprintf(tagbuf, sizeof(tagbuf), "Slot %d", s);
                tag = tagbuf;
            } else {
                std::snprintf(tagbuf, sizeof(tagbuf), "Auto %d", s - autoBase + 1);
                tag = tagbuf;
            }
            ImGui::Text("%s", tag);

            // Column 2: Actions
            ImGui::TableSetColumnIndex(1);
            if (ImGui::Button("Save")) {
                if (Port_QuickSave_SaveSlot(s))
                    Port_DebugMenu_ToastFromExternal("Saved");
            }
            ImGui::SameLine();
            if (Port_QuickSave_HasSlot(s)) {
                if (ImGui::Button("Load")) {
                    if (Port_QuickSave_LoadSlot(s))
                        Port_DebugMenu_ToastFromExternal("Loaded");
                }
            } else {
                ImGui::BeginDisabled();
                ImGui::Button("Load");
                ImGui::EndDisabled();
            }

            // Column 3: Timestamp
            ImGui::TableSetColumnIndex(2);
            unsigned long long ts = Port_QuickSave_SlotTimestamp(s);
            if (ts == 0) {
                ImGui::TextDisabled("(empty)");
            } else {
                time_t tt = (time_t)ts;
                struct tm tm_buf;
#ifdef _WIN32
                localtime_s(&tm_buf, &tt);
#else
                localtime_r(&tt, &tm_buf);
#endif
                char timestr[32];
                std::strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", &tm_buf);
                ImGui::TextDisabled("%s", timestr);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::Separator();
    (void)DrawRegionLanguageControls(false);
}
