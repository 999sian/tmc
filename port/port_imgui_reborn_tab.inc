/* Reborn ribbon tab — extracted from port_imgui_menu.cpp. Included into the same
 * translation unit; relies on the declarations already visible there. */
static void DrawRibbonRebornTab(void) {
    ImGui::TextWrapped("Quality-of-life features ported from Minish Cap Reborn "
                       "(GPL-3.0); see THIRD-PARTY-LICENSES.md. Toggles persist "
                       "until tmc_pc closes.");
    ImGui::Separator();
    for (int i = 0; i < REBORN_FEAT_COUNT; ++i) {
        /* Slot 8 (rupee-like overhaul) was removed; its enum slot is kept so
         * the persisted feature bitmask (issue #146) stays stable, but it has
         * no behaviour and is hidden from this tab. */
        if (i == REBORN_FEAT_RUPEE_LIKE_OVERHAUL)
            continue;
        bool on = Port_Reborn_IsEnabled((RebornFeature)i);
        const char* label = Port_Reborn_FeatureLabel((RebornFeature)i);
        const char* desc = Port_Reborn_FeatureDescription((RebornFeature)i);
        if (ImGui::Checkbox(label, &on)) {
            Port_Reborn_SetEnabled((RebornFeature)i, on);
        }
        if (desc && desc[0]) {
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(360.0f);
                ImGui::TextUnformatted(desc);
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
    }

    /* Deadzone tuning for the 360° analog feature above — only meaningful
     * while it's enabled, so the slider appears indented underneath it. */
    if (Port_Reborn_IsEnabled(REBORN_FEAT_ANALOG_360_MOVEMENT)) {
        ImGui::Indent(20.0f);
        float dz = Port_Config_GetAnalogDeadzone();
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::SliderFloat("Analog deadzone", &dz, 0.0f, 0.95f, "%.2f")) {
            Port_Config_SetAnalogDeadzone(dz);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(360.0f);
            ImGui::TextUnformatted("Left-stick displacement below this fraction is ignored, so the "
                                   "D-pad stays authoritative and a thumb resting on the stick won't "
                                   "drift. Raise it for a worn/drifty stick; lower it for a lighter "
                                   "touch. Default 0.30.");
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        ImGui::Unindent(20.0f);
    }
}
