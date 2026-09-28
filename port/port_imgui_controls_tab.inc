/* Controls ribbon tab — extracted from port_imgui_menu.cpp. Included into the same
 * translation unit; relies on the declarations already visible there. */
/* Friendly display name for each action — matches the GBA button names
 * users actually think in. The Port_Config side stores them as
 * short ids ("a", "b", "soft_l2") for config.json compactness. */
static const char* InputLabel(int input) {
    switch (input) {
        case PORT_INPUT_A:
            return "A button (action)";
        case PORT_INPUT_B:
            return "B button (sword)";
        case PORT_INPUT_SELECT:
            return "Select";
        case PORT_INPUT_START:
            return "Start (pause)";
        case PORT_INPUT_RIGHT:
            return "D-pad Right";
        case PORT_INPUT_LEFT:
            return "D-pad Left";
        case PORT_INPUT_UP:
            return "D-pad Up";
        case PORT_INPUT_DOWN:
            return "D-pad Down";
        case PORT_INPUT_R:
            return "R (item slot 2)";
        case PORT_INPUT_L:
            return "L (item slot 1)";
        case PORT_INPUT_SOFT_X:
            return "Soft slot X";
        case PORT_INPUT_SOFT_Y:
            return "Soft slot Y";
        case PORT_INPUT_SOFT_L2:
            return "Soft slot L2";
        case PORT_INPUT_SOFT_R2:
            return "Soft slot R2";
        case PORT_INPUT_ROLL_ATTACK:
            return "Roll attack (D / R3)";
        default:
            return Port_Config_InputName((PortInput)input);
    }
}

static void DrawRibbonControlsTab(void) {
    if (ImGui::CollapsingHeader("Keyboard shortcuts")) {
        struct HotkeyRow {
            const char* key;
            const char* action;
        };
        static const HotkeyRow kHotkeys[] = {
            { "F8", "Open / close this settings menu (gamepad: Select+Start)" },
            { "F5 / F6", "Quicksave / quickload" },
            { "F1-F4", "Load save-state slot 1-4  (Shift+Fn = save to slot)" },
            { "F7", "Toggle text-to-speech" },
            { "F9", "Capture a bug report (screenshot + save + state)" },
            { "F10", "Speak nearby points of interest  (Shift: next, Ctrl: orient)" },
            { "F11 / Alt+Enter", "Toggle fullscreen" },
            { "F12", "Cycle the display filter / smoothing" },
            { "Tab (hold)", "Fast-forward" },
            { "[  ]", "Practice: set / reload practice point" },
            { "P  .", "Practice: pause / frame-advance while paused" },
            { "'  ;", "Practice: reset timer / record split" },
        };
        if (ImGui::BeginTable("##hotkeys", 2,
                              ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
            for (const HotkeyRow& row : kHotkeys) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "%s", row.key);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(row.action);
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("Save-states (F1-F6) are disabled in Console-Parity mode.");
    }
#ifdef __ANDROID__
    if (ImGui::CollapsingHeader("Touch controls", ImGuiTreeNodeFlags_DefaultOpen)) {
        {
            int scheme = (Port_Config_TouchScheme() == PORT_TOUCH_SCHEME_DPAD) ? 1 : 0;
            const char* items[] = { "Joystick (floating)", "D-pad" };
            if (ImGui::Combo("Movement", &scheme, items, 2)) {
                Port_Config_SetTouchScheme(scheme == 1 ? PORT_TOUCH_SCHEME_DPAD : PORT_TOUCH_SCHEME_JOYSTICK);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Joystick: touch anywhere on the lower-left of the screen to "
                                  "plant the stick there.\nD-pad: fixed four-way pad.");
            }
        }
        {
            float v = Port_Config_TouchScale();
            if (ImGui::SliderFloat("Button size", &v, 0.6f, 1.6f, "%.2fx")) {
                Port_Config_SetTouchScale(v);
            }
        }
        {
            float v = Port_Config_TouchOpacity();
            if (ImGui::SliderFloat("Overlay opacity", &v, 0.3f, 1.5f, "%.2fx")) {
                Port_Config_SetTouchOpacity(v);
            }
        }
        ImGui::TextDisabled("The R button glows green when it has an action (talk, read, lift...).");
    }
    ImGui::Separator();
#endif
    {
        bool on = Port_Config_GetRollAttackMacroEnabled();
        if (ImGui::Checkbox("Roll attack macro", &on)) {
            Port_Config_SetRollAttackMacroEnabled(on);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Hold a direction and press the Roll attack bind to perform a "
                              "start-of-roll attack with your best sword, regardless of A/B "
                              "equip.\nDefault: keyboard D, controller R3 (right stick click).");
        }
    }
    ImGui::Separator();
    ImGui::TextWrapped("Click 'Set' to replace an action's binding, or 'Add' to bind an extra "
                       "key/controller button to it, then press the input. Esc cancels. Mappings "
                       "save to config.json automatically. In Console-Parity mode each physical "
                       "input maps to only one action.");
    ImGui::Separator();

    /* Two-column-ish table: action label | bindings + buttons. */
    if (ImGui::BeginTable("##controls", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 200.0f);
        ImGui::TableSetupColumn("Bindings", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, 180.0f);

        for (int i = 0; i < PORT_INPUT_COUNT; ++i) {
            ImGui::PushID(i);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(InputLabel(i));

            ImGui::TableSetColumnIndex(1);
            const int n = Port_Config_BindingCount((PortInput)i);
            if (n == 0) {
                ImGui::TextDisabled("(unbound)");
            } else {
                for (int b = 0; b < n; ++b) {
                    char label[64];
                    Port_Config_BindingLabel((PortInput)i, b, label, sizeof(label));
                    if (b > 0)
                        ImGui::SameLine(0, 6);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.30f, 0.45f, 1.0f));
                    ImGui::Button(label);
                    ImGui::PopStyleColor();
                }
            }

            ImGui::TableSetColumnIndex(2);
            if (ImGui::Button("Set")) {
                Port_Config_BeginCaptureBinding((PortInput)i);
                ImGui::OpenPopup("Capture binding");
            }
            ImGui::SameLine();
            if (ImGui::Button("Add")) {
                Port_Config_BeginAddBinding((PortInput)i);
                ImGui::OpenPopup("Capture binding");
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear")) {
                Port_Config_ClearBindings((PortInput)i);
            }

            /* Modal popup that hangs around until the capture path
             * commits (which clears IsCapturingBinding). We render the
             * popup per-row but OpenPopup is fine because only one is
             * open at a time. */
            ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            if (ImGui::BeginPopupModal("Capture binding", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
                ImGui::Text("Press a key or controller button for:");
                ImGui::TextColored(ImVec4(1, 0.94f, 0.25f, 1), "%s", InputLabel(i));
                ImGui::Text("Esc cancels.");
                ImGui::Separator();
                if (ImGui::Button("Cancel") || !Port_Config_IsCapturingBinding()) {
                    Port_Config_CancelCaptureBinding();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.30f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.40f, 0.30f, 1.0f));
    if (ImGui::Button("Reset all to defaults")) {
        Port_Config_ResetAllBindings();
        Port_DebugMenu_ToastFromExternal("Bindings reset");
    }
    ImGui::PopStyleColor(2);
}
