/* Warp ribbon tab — extracted from port_imgui_menu.cpp. Included into the same
 * translation unit; relies on the declarations already visible there. */
static char sWarpFilter[64] = "";

/* Override lookup from port_debug_actions.c — returns 1 + fills x/y/layer
 * when (area, room) has a curated safe-spawn entry, else 0. Used by the
 * Warp tab so high-traffic rooms whose geometric center is a wall (boss
 * arenas, dungeon entrances, town buildings) drop Link on walkable
 * ground instead of an obstacle. See issue #94. */
/* Returns 1 if (area) is safe to warp to (has a friendly name and is
 * not on the known-broken deny-list). Same predicate the dispatch
 * layer uses, so the UI list and the action layer agree on what's
 * warpable. See port_debug_actions.c::kBrokenWarpAreas. */

static void DrawRibbonWarpTab(void) {
    /* Filter bar — type to narrow the area list. Empty filter = show
     * everything. Case-insensitive substring match. Steam Deck users
     * can ignore the filter and just scroll. */
    ImGui::SetNextItemWidth(280);
    ImGui::InputTextWithHint("##warpFilter", "filter by area name (e.g. 'castle')", sWarpFilter, sizeof(sWarpFilter));
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
        sWarpFilter[0] = '\0';
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextDisabled("L/R bumpers = page jump");

    /* Free-coordinate teleport within the CURRENT room. Pre-fills from Link's
     * live position; in-game only. (All primitives already exist - this is the
     * same write WarpTick does.) */
    ImGui::Separator();
    {
        static int sTeleX = 0, sTeleY = 0;
        unsigned short px = 0, py = 0;
        const bool inGame = Port_DebugQuery_PlayerXY(&px, &py) != 0;
        ImGui::TextUnformatted("Teleport (current room):");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        ImGui::InputInt("X##tele", &sTeleX, 0);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        ImGui::InputInt("Y##tele", &sTeleY, 0);
        ImGui::BeginDisabled(!inGame);
        ImGui::SameLine();
        if (ImGui::Button("Go##tele")) {
            unsigned short tx = (unsigned short)(sTeleX < 0 ? 0 : sTeleX);
            unsigned short ty = (unsigned short)(sTeleY < 0 ? 0 : sTeleY);
            if (Port_DebugAction_TeleportXY(tx, ty)) {
                char msg[64];
                std::snprintf(msg, sizeof(msg), "Teleport -> (%u, %u)", tx, ty);
                Port_DebugMenu_ToastFromExternal(msg);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Use Link's pos")) {
            sTeleX = px;
            sTeleY = py;
        }
        ImGui::EndDisabled();
        if (!inGame) {
            ImGui::SameLine();
            ImGui::TextDisabled("(in-game only)");
        }
    }
    {
        bool noclip = Port_DebugQuery_Noclip() != 0;
        if (ImGui::Checkbox("Noclip (walk through walls)", &noclip)) {
            Port_DebugAction_SetNoclip(noclip ? 1 : 0);
        }
        if (Port_Config_GetConsoleParity()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(disabled in Console-Parity)");
        }
    }
    {
        unsigned short px = 0, py = 0;
        const bool inGame = Port_DebugQuery_PlayerXY(&px, &py) != 0;
        const bool isMinish = inGame && (Port_DebugQuery_IsMinish() != 0);
        ImGui::BeginDisabled(!inGame);
        if (ImGui::Button(isMinish ? "Grow to Normal Size" : "Shrink to Minish")) {
            Port_DebugAction_ToggleMinish();
        }
        ImGui::EndDisabled();
        if (!inGame) {
            ImGui::SameLine();
            ImGui::TextDisabled("(in-game only)");
        }
    }
    ImGui::Separator();

    /* Letter strip — issue #76. The area list is long (>140 entries)
     * and previously the only way to traverse was one-line-at-a-time
     * scrolling. Buttons here filter to areas whose name starts with
     * that letter, giving instant A-Z jumps. */
    static char sLetterFilter = 0; /* 0 = no letter filter */
    {
        const char* kLetters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        if (ImGui::SmallButton("All##warpLet"))
            sLetterFilter = 0;
        for (const char* p = kLetters; *p; ++p) {
            ImGui::SameLine();
            char id[6];
            std::snprintf(id, sizeof(id), "%c##wl", *p);
            if (sLetterFilter == *p) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.55f, 0.30f, 1.0f));
                if (ImGui::SmallButton(id))
                    sLetterFilter = 0;
                ImGui::PopStyleColor();
            } else {
                if (ImGui::SmallButton(id))
                    sLetterFilter = *p;
            }
        }
    }

    /* Scrollable area list. Each area is a collapsible header that
     * reveals its rooms as a button grid when opened. Headers are
     * controller-navigable (D-pad up/down) and the inner buttons take
     * keyboard / gamepad nav too. */
    ImGui::Separator();
    const float listH = ImGui::GetContentRegionAvail().y - 4.0f;
    if (ImGui::BeginChild("##warpList", ImVec2(0, listH), ImGuiChildFlags_NavFlattened, 0)) {
        /* L1/R1 bumpers = page jump while this child is focused/hovered.
         * PgUp / PgDn keys give the same shortcut on keyboard. Home /
         * End jump to the ends of the list. Issue #76. */
        const bool listFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
                                 ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
        if (listFocused) {
            const float pageStep = listH * 0.9f;
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false) || ImGui::IsKeyPressed(ImGuiKey_PageUp, false)) {
                ImGui::SetScrollY(ImGui::GetScrollY() - pageStep);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false) || ImGui::IsKeyPressed(ImGuiKey_PageDown, false)) {
                ImGui::SetScrollY(ImGui::GetScrollY() + pageStep);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
                ImGui::SetScrollY(0.0f);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_End, false)) {
                ImGui::SetScrollY(ImGui::GetScrollMaxY());
            }
        }
        const std::string filter = sWarpFilter[0] ? std::string(sWarpFilter) : std::string();
        std::string filter_lower = filter;
        for (auto& c : filter_lower)
            c = (char)std::tolower((unsigned char)c);

        int shown = 0;
        for (unsigned int area = 0; area < 0x90; ++area) {
            unsigned char a = (unsigned char)area;
            int roomCount = Port_DebugQuery_AreaRoomCount(a);
            if (roomCount <= 0)
                continue;

            const char* name = Port_DebugQuery_AreaName(a);
            /* Skip areas that aren't warpable: no friendly name (AREA_
             * NULL_*, numeric AREA_40-style slots) OR named-but-known-
             * broken (Simon's Sim, etc.). Same predicate the dispatch
             * layer uses so list + action stay in sync. See issue #94. */
            if (!Port_DebugAction_AreaIsWarpable(a))
                continue;
            char header[96];
            std::snprintf(header, sizeof(header), "0x%02X  %s  (%d rooms)", area, name, roomCount);

            if (!filter_lower.empty()) {
                std::string hl(header);
                for (auto& c : hl)
                    c = (char)std::tolower((unsigned char)c);
                if (hl.find(filter_lower) == std::string::npos)
                    continue;
            }
            /* Letter strip — match against the first letter of the
             * area name itself (skip the "0xNN  " prefix). */
            if (sLetterFilter && name) {
                char first = name[0];
                if (first >= 'a' && first <= 'z')
                    first = (char)(first - 'a' + 'A');
                if (first != sLetterFilter)
                    continue;
            }
            shown++;

            ImGui::PushID((int)area);
            if (ImGui::CollapsingHeader(header)) {
                ImGui::Indent();
                /* Per-room buttons in a 3-column grid for compactness. Room 0
                 * is simply the first grid entry below — there is no separate
                 * "Warp here (room 0)" button, which duplicated it (v0.6). */
                int col = 0;
                for (int r = 0; r < roomCount; ++r) {
                    unsigned short w = 0, h = 0;
                    if (!Port_DebugQuery_RoomDimensions(a, (unsigned char)r, &w, &h))
                        continue;
                    char roomLabel[48];
                    std::snprintf(roomLabel, sizeof(roomLabel), "Room 0x%02X", r);
                    ImGui::PushID(r);
                    if (ImGui::Button(roomLabel, ImVec2(120, 0))) {
                        unsigned short cx = 0, cy = 0;
                        unsigned char layer = 1;
                        if (!Port_DebugAction_WarpSpawnOverride(a, (unsigned char)r, &cx, &cy, &layer)) {
                            cx = w ? (unsigned short)(w / 2) : 0x80;
                            cy = h ? (unsigned short)(h / 2) : 0x80;
                            layer = 1;
                        }
                        if (Port_DebugAction_Warp(a, (unsigned char)r, cx, cy, layer)) {
                            char msg[96];
                            std::snprintf(msg, sizeof(msg), "Warp -> 0x%02X room 0x%02X", area, r);
                            Port_DebugMenu_ToastFromExternal(msg);
                        } else {
                            Port_DebugMenu_ToastFromExternal("Warp ignored: not in gameplay");
                        }
                    }
                    ImGui::PopID();
                    if ((col % 3) != 2)
                        ImGui::SameLine();
                    col++;
                }
                ImGui::Unindent();
            }
            ImGui::PopID();
        }
        if (shown == 0) {
            ImGui::TextDisabled("No areas match the filter.");
        }
    }
    ImGui::EndChild();
}
