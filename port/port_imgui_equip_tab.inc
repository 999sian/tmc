/* Equip ribbon tab — extracted from port_imgui_menu.cpp. Included into the same
 * translation unit; relies on the declarations already visible there. */
/* The < / > soft-slot assignment cycler, shared by the Equip tab and the
 * in-game soft-slot config overlay (caller draws the surrounding row). */
static void DrawSoftSlotCycleButtons(int slot) {
    if (ImGui::Button("<"))
        Port_SoftSlots_CycleAssignment(slot, -1);
    ImGui::SameLine();
    if (ImGui::Button(">"))
        Port_SoftSlots_CycleAssignment(slot, +1);
}

static void DrawRibbonEquipTab(void) {
    if (ImGui::BeginTable("##equip_table", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Assigned Item", ImGuiTableColumnFlags_WidthFixed, 220.0f);
        ImGui::TableSetupColumn("Cycle", ImGuiTableColumnFlags_WidthStretch);

        for (int s = 0; s < 4; ++s) {
            ImGui::PushID(s);
            ImGui::TableNextRow();

            // Column 1: Slot name
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", Port_SoftSlots_SlotName(s));

            // Column 2: Assigned item name
            ImGui::TableSetColumnIndex(1);
            const char* label = Port_SoftSlots_GetSlotLabel(s);
            const char* colon = std::strchr(label, ':');
            const char* item_name = colon ? colon + 1 : label;
            while (*item_name == ' ')
                ++item_name;
            ImGui::Text("%s", item_name);

            // Column 3: Buttons
            ImGui::TableSetColumnIndex(2);
            DrawSoftSlotCycleButtons(s);

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}
