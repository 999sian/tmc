/* Memory ribbon tab — extracted from port_imgui_menu.cpp. Included into the same
 * translation unit; relies on the declarations already visible there. */
/* Live memory-watch tab (#feature). Type a GBA address + width, "Add watch",
 * and each entry shows its value live every frame. Reads are fault-safe
 * (Port_DebugQuery_MemRead), so an unmapped address renders "<unmapped>"
 * rather than faulting. Session-only — watches are intentionally not persisted
 * (a stale address from a previous build would be misleading). */
static void DrawRibbonMemoryTab(void) {
    ImGui::TextUnformatted("Watch arbitrary GBA memory live: EWRAM 0x02xxxxxx, IWRAM 0x03xxxxxx, "
                           "I/O 0x04xxxxxx, palette 0x05xxxxxx, VRAM 0x06xxxxxx, OAM 0x07xxxxxx, ROM 0x08xxxxxx.");
    ImGui::TextDisabled("Session-only: watches are not saved to config.");
    ImGui::Separator();

    static char sAddrBuf[16] = "03000000";
    static int sWidth = 0; /* 0=u8 1=u16 2=u32 */
    static const char* const kWidthNames[3] = { "u8", "u16", "u32" };

    ImGui::SetNextItemWidth(110);
    ImGui::InputText("##memaddr", sAddrBuf, sizeof(sAddrBuf),
                     ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(64);
    ImGui::Combo("##memwidth", &sWidth, kWidthNames, 3);
    ImGui::SameLine();

    const unsigned int candAddr = (unsigned int)strtoul(sAddrBuf, nullptr, 16);
    unsigned int candVal = 0;
    const int candOk = Port_DebugQuery_MemRead(candAddr, sWidth, &candVal);
    if (ImGui::Button("Add watch")) {
        if (Port_DebugAction_MemWatchAdd(candAddr, sWidth) < 0) {
            Port_DebugMenu_ToastFromExternal("Watch list full (32 max)");
        }
    }
    ImGui::SameLine();
    if (candOk) {
        ImGui::Text("= 0x%0*X  (%u)", (1 << sWidth) * 2, candVal, candVal);
    } else {
        ImGui::TextDisabled("= <unmapped>");
    }

    ImGui::Separator();
    const int n = Port_DebugQuery_MemWatchCount();
    ImGui::Text("Watches: %d / 32", n);
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear all"))
        Port_DebugAction_MemWatchClear();

    if (ImGui::BeginTable("##memwatch", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_SizingFixedFit,
                          ImVec2(0, 280))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Address");
        ImGui::TableSetupColumn("Width");
        ImGui::TableSetupColumn("Value");
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        int removeIdx = -1;
        for (int i = 0; i < n; ++i) {
            const unsigned int a = Port_DebugQuery_MemWatchAddr(i);
            const int w = Port_DebugQuery_MemWatchWidth(i);
            unsigned int v = 0;
            const int ok = Port_DebugQuery_MemRead(a, w, &v);
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("0x%08X", a);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(Port_DebugQuery_MemWidthName(w));
            ImGui::TableNextColumn();
            if (ok)
                ImGui::Text("0x%0*X  (%u)", (1 << w) * 2, v, v);
            else
                ImGui::TextDisabled("<unmapped>");
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("X"))
                removeIdx = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (removeIdx >= 0)
            Port_DebugAction_MemWatchRemove(removeIdx);
    }
}
