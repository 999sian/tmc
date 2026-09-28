/* Entities ribbon tab — extracted from port_imgui_menu.cpp. Included into the same
 * translation unit; relies on the declarations already visible there. */
/* Read-only entity viewer (#feature). Snapshots all live entities each frame
 * via the recycled-node-safe walk in port_debug_entities.c and lists them in a
 * scrollable table (clipped, so a full 72-entity room is cheap). */
static void DrawRibbonEntitiesTab(void) {
    const int n = Port_DebugQuery_RefreshEntities();
    ImGui::Text("Live entities: %d", n);
    ImGui::SameLine();
    ImGui::TextDisabled("(snapshot, refreshed each frame)");
    if (ImGui::BeginTable("##entities", 6,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_SizingFixedFit,
                          ImVec2(0, 320))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("List");
        ImGui::TableSetupColumn("Kind");
        ImGui::TableSetupColumn("id");
        ImGui::TableSetupColumn("type");
        ImGui::TableSetupColumn("x, y");
        ImGui::TableSetupColumn("hp");
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(n);
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const PortEntityInfo* e = Port_DebugQuery_Entity(i);
                if (!e)
                    continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", e->listIndex);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(Port_DebugQuery_EntityKindName(e->kind));
                ImGui::TableNextColumn();
                ImGui::Text("0x%02X", (unsigned)e->id);
                ImGui::TableNextColumn();
                ImGui::Text("0x%02X", (unsigned)e->type);
                ImGui::TableNextColumn();
                ImGui::Text("%d, %d", e->x, e->y);
                ImGui::TableNextColumn();
                ImGui::Text("%u", (unsigned)e->health);
            }
        }
        ImGui::EndTable();
    }
}
