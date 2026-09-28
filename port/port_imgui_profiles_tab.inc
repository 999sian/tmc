/* Profiles ribbon tab — extracted from port_imgui_menu.cpp. Included into the same
 * translation unit; relies on the declarations already visible there. */
static void DrawRibbonProfilesTab(void) {
    char names[32][64];
    const int n = Port_Save_ListProfiles(names, 32);
    const std::string activeNow = Port_Save_GetActivePath();

    ImGui::Text("Active profile: %s", activeNow.c_str());
    ImGui::Separator();

    /* Rename buffer keyed by index, so each row has its own inline
     * editor that survives across frames while the user is typing. */
    static char sRenameBuf[32][64] = {};
    static int sRenameRow = -1;
    static int sConfirmDeleteRow = -1;

    if (ImGui::BeginTable("##profiles_table", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Profile File", ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthStretch);

        for (int i = 0; i < n; ++i) {
            ImGui::PushID(i);
            ImGui::TableNextRow();

            // Column 1: Profile Name
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", names[i]);

            // Column 2: Status
            ImGui::TableSetColumnIndex(1);
            bool isActive = (std::string(names[i]) == activeNow);
            if (isActive) {
                ImGui::TextColored(ImVec4(1.0f, 0.94f, 0.25f, 1.0f), "active");
            } else {
                ImGui::TextDisabled("-");
            }

            // Column 3: Actions
            ImGui::TableSetColumnIndex(2);
            if (!isActive) {
                if (ImGui::Button("Activate")) {
                    if (Port_Save_SetActivePath(names[i])) {
                        Port_Config_SetActiveSaveProfile(names[i]);
                        Port_DebugMenu_ToastFromExternal("Profile activated - go to title to load");
                    } else {
                        Port_DebugMenu_ToastFromExternal("Cannot switch profiles: current save could not be written");
                    }
                }
                ImGui::SameLine();
            }
            const bool isDefault = (std::strcmp(names[i], "tmc.sav") == 0);
            if (!isDefault) {
                if (ImGui::Button("Rename")) {
                    sRenameRow = i;
                    snprintf(sRenameBuf[i], sizeof(sRenameBuf[i]), "%s", names[i]);
                }
                ImGui::SameLine();
                if (ImGui::Button("Delete"))
                    sConfirmDeleteRow = i;
            }

            if (sRenameRow == i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushItemWidth(160);
                ImGui::InputText("##rename", sRenameBuf[i], sizeof(sRenameBuf[i]));
                ImGui::PopItemWidth();
                ImGui::TableSetColumnIndex(2);
                if (ImGui::Button("OK")) {
                    if (Port_Save_RenameProfile(names[i], sRenameBuf[i])) {
                        Port_DebugMenu_ToastFromExternal("Profile renamed");
                    } else {
                        Port_DebugMenu_ToastFromExternal("Rename refused (clash / bad name / default)");
                    }
                    sRenameRow = -1;
                }
                ImGui::SameLine();
                if (ImGui::Button("X"))
                    sRenameRow = -1;
            }
            if (sConfirmDeleteRow == i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(ImVec4(0.9f, 0.35f, 0.35f, 1.0f), "Delete %s?", names[i]);
                ImGui::TableSetColumnIndex(2);
                if (ImGui::Button("Confirm delete")) {
                    if (Port_Save_DeleteProfile(names[i])) {
                        Port_DebugMenu_ToastFromExternal("Profile deleted");
                    } else {
                        Port_DebugMenu_ToastFromExternal("Delete refused (active / bad name)");
                    }
                    sConfirmDeleteRow = -1;
                    sRenameRow = -1;
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                    sConfirmDeleteRow = -1;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    if (ImGui::Button("+ Save current as new profile")) {
        char name[64];
        int k = 1;
        for (; k <= 99; ++k) {
            std::snprintf(name, sizeof(name), "tmc_%d.sav", k);
            FILE* probe = std::fopen(name, "rb");
            if (!probe)
                break;
            std::fclose(probe);
        }
        if (k > 99)
            Port_DebugMenu_ToastFromExternal("No free profile slots (1-99)");
        else if (Port_Save_SaveAsProfile(name)) {
            char msg[96];
            std::snprintf(msg, sizeof(msg), "Saved current as %s", name);
            Port_DebugMenu_ToastFromExternal(msg);
        } else {
            Port_DebugMenu_ToastFromExternal("Save failed");
        }
    }
}
