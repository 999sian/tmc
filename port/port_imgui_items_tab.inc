/* Items ribbon tab — extracted from port_imgui_menu.cpp. Included into the same
 * translation unit; relies on the declarations already visible there. */
/* ---- Feature 1 (per-item toggle) + 6 (charm / picolyte) ribbon widgets ----
 * All game-state knowledge lives in port_debug_actions.c; these helpers only
 * enumerate the C layer (index/name/group) and drive the corresponding
 * Set/Query actions, so no ITEM_* logic crosses into this C++ TU beyond the
 * C++-safe enum constants from item_ids.h. */

/* Per-item ownership grid. Items arrive from the C layer already grouped
 * (contiguous by group string); each group becomes a collapsible header so
 * the long list doesn't dominate the Items tab. Each checkbox reflects live
 * ownership, so slot-exclusivity clears show up on the next frame. */
static void DrawRibbonItemToggles(void) {
    const int count = Port_DebugQuery_ToggleItemCount();
    const char* curGroup = nullptr;
    bool groupOpen = false;
    for (int i = 0; i < count; ++i) {
        const char* group = Port_DebugQuery_ToggleItemGroup(i);
        if (!group)
            continue;
        if (!curGroup || strcmp(group, curGroup) != 0) {
            curGroup = group;
            groupOpen = ImGui::CollapsingHeader(group);
        }
        if (!groupOpen)
            continue;
        bool owned = Port_DebugQuery_ToggleItemOwned(i) != 0;
        ImGui::PushID(i);
        if (ImGui::Checkbox(Port_DebugQuery_ToggleItemName(i), &owned)) {
            Port_DebugAction_SetToggleItem(i, owned ? 1 : 0);
        }
        ImGui::PopID();
    }
}

/* Stable per-frame label for dungeon id d, marking the current dungeon. */
static const char* DungeonLabel(int d, int cur) {
    static char buf[40];
    snprintf(buf, sizeof(buf), "Dungeon %d%s", d, (d == cur) ? "  (current)" : "");
    return buf;
}

/* Any-dungeon Map / Compass / Big Key / Small Key editor. The arrays are
 * indexed by dungeon id; the engine only ever writes the current area's
 * slot, so we write them directly to reach any dungeon. */
static void DrawRibbonDungeonItems(void) {
    static int sDungeon = 0;
    const int cur = Port_DebugQuery_CurrentDungeon();
    if (sDungeon < 0 || sDungeon > 15)
        sDungeon = 0;

    ImGui::SetNextItemWidth(180);
    if (ImGui::BeginCombo("Dungeon", DungeonLabel(sDungeon, cur))) {
        for (int d = 0; d < 16; ++d) {
            const bool sel = (d == sDungeon);
            ImGui::PushID(d);
            if (ImGui::Selectable(DungeonLabel(d, cur), sel))
                sDungeon = d;
            if (sel)
                ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (cur >= 0) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Jump to current"))
            sDungeon = cur;
    }

    const int bits = Port_DebugQuery_DungeonItems(sDungeon);
    bool map = (bits & 0x1) != 0;
    bool comp = (bits & 0x2) != 0;
    bool big = (bits & 0x4) != 0;
    if (ImGui::Checkbox("Map", &map))
        Port_DebugAction_SetDungeonItem(sDungeon, 0, map);
    ImGui::SameLine();
    if (ImGui::Checkbox("Compass", &comp))
        Port_DebugAction_SetDungeonItem(sDungeon, 1, comp);
    ImGui::SameLine();
    if (ImGui::Checkbox("Big Key", &big))
        Port_DebugAction_SetDungeonItem(sDungeon, 2, big);

    int keys = Port_DebugQuery_DungeonKeys(sDungeon);
    ImGui::SetNextItemWidth(120);
    if (ImGui::InputInt("Small keys", &keys)) {
        if (keys < 0)
            keys = 0;
        if (keys > 255)
            keys = 255;
        Port_DebugAction_SetDungeonKeys(sDungeon, keys);
    }
}

extern "C" {
typedef void (*PortBuffApplyFn)(int, int);
typedef int (*PortBuffQueryFn)(int*, int*);
}

/* Charm and picolyte share the same combo + frames-slider + Apply + live-
 * status shape; only the labels, id list, default state, and apply/query
 * hooks differ. Selection + frames state is caller-owned so the two buffs
 * don't share it. */
static void DrawTimedBuff(const char* label, const char* lname, const char* applyId, const char** names, const int* ids,
                          int count, int* sel, int* frames, PortBuffApplyFn apply, PortBuffQueryFn query) {
    char tag[32];
    ImGui::SetNextItemWidth(200);
    std::snprintf(tag, sizeof(tag), "%s type", label);
    ImGui::Combo(tag, sel, names, count);
    ImGui::SetNextItemWidth(200);
    std::snprintf(tag, sizeof(tag), "%s frames", label);
    ImGui::SliderInt(tag, frames, 0, 65535, "%d", ImGuiSliderFlags_Logarithmic);
    ImGui::SameLine();
    std::snprintf(tag, sizeof(tag), "Apply##%s", applyId);
    if (ImGui::Button(tag)) {
        apply(ids[*sel], *frames);
        char toast[40];
        std::snprintf(toast, sizeof(toast), "%s %s", label, *sel == 0 ? "cleared" : "applied");
        Port_DebugMenu_ToastFromExternal(toast);
    }
    int id = 0, timer = 0;
    if (query(&id, &timer))
        ImGui::TextDisabled("%s active: id %d, %d frames (%.1fs left)", lname, id, timer, timer / 60.0f);
    else
        ImGui::TextDisabled("%s: inactive", lname);
}

/* Charm + Picolyte activator. The combo + slider compose a buff to apply on
 * the button; the live line shows what's currently ticking (the engine
 * counts the timer down each frame, so the slider isn't bound to it). */
static void DrawRibbonBuffs(void) {
    static const char* kCharmNames[] = { "Off", "Nayru (1/4 dmg taken)", "Farore (1/2 dmg taken)",
                                         "Din (2x dmg dealt)" };
    static const int kCharmIds[] = { 0, BOTTLE_CHARM_NAYRU, BOTTLE_CHARM_FARORE, BOTTLE_CHARM_DIN };
    static int sCharmSel = 1;
    static int sCharmFrames = 3600;
    DrawTimedBuff("Charm", "charm", "charm", kCharmNames, kCharmIds, IM_ARRAYSIZE(kCharmNames), &sCharmSel,
                  &sCharmFrames, Port_DebugAction_SetCharm, Port_DebugQuery_Charm);

    ImGui::Spacing();

    static const char* kPicoNames[] = { "Off", "Red", "Orange", "Yellow", "Green", "Blue", "White" };
    static const int kPicoIds[] = { 0,
                                    ITEM_BOTTLE_PICOLYTE_RED,
                                    ITEM_BOTTLE_PICOLYTE_ORANGE,
                                    ITEM_BOTTLE_PICOLYTE_YELLOW,
                                    ITEM_BOTTLE_PICOLYTE_GREEN,
                                    ITEM_BOTTLE_PICOLYTE_BLUE,
                                    ITEM_BOTTLE_PICOLYTE_WHITE };
    static int sPicoSel = 1;
    static int sPicoFrames = 900;
    DrawTimedBuff("Picolyte", "picolyte", "pico", kPicoNames, kPicoIds, IM_ARRAYSIZE(kPicoNames), &sPicoSel,
                  &sPicoFrames, Port_DebugAction_SetPicolyte, Port_DebugQuery_Picolyte);
}

/* Numeric count / capacity sliders. Bounds come from the C layer (counts clamp
 * to the live capacity tier), so each row is a plain min..max slider. */
static void DrawRibbonStats(void) {
    const int count = Port_DebugQuery_StatCount();
    for (int i = 0; i < count; ++i) {
        int v = Port_DebugQuery_StatValue(i);
        const int lo = Port_DebugQuery_StatMin(i);
        const int hi = Port_DebugQuery_StatMax(i);
        ImGui::PushID(i);
        ImGui::SetNextItemWidth(220);
        if (ImGui::SliderInt(Port_DebugQuery_StatName(i), &v, lo, hi)) {
            Port_DebugAction_SetStat(i, v);
        }
        ImGui::PopID();
    }
}

/* Per-bottle content picker. Choosing a content also grants the bottle. */
static void DrawRibbonBottles(void) {
    const int nContents = Port_DebugQuery_BottleContentCount();
    for (int b = 0; b < 4; ++b) {
        ImGui::PushID(b);
        const bool owned = Port_DebugQuery_BottleOwned(b) != 0;
        const int curIdx = Port_DebugQuery_BottleContentIndex(Port_DebugQuery_BottleContent(b));
        char label[16];
        snprintf(label, sizeof(label), "Bottle %d", b + 1);
        ImGui::SetNextItemWidth(200);
        if (ImGui::BeginCombo(label, Port_DebugQuery_BottleContentName(curIdx))) {
            for (int i = 0; i < nContents; ++i) {
                const bool sel = (i == curIdx);
                if (ImGui::Selectable(Port_DebugQuery_BottleContentName(i), sel)) {
                    Port_DebugAction_SetBottleContent(b, Port_DebugQuery_BottleContentId(i));
                }
                if (sel)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (!owned) {
            ImGui::SameLine();
            ImGui::TextDisabled("(not owned — pick to grant)");
        }
        ImGui::PopID();
    }
}

static void DrawRibbonItemsTab(void) {
    if (ImGui::BeginTable("##items_tab_table", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Stats & Unlocks", ImGuiTableColumnFlags_WidthFixed, 220.0f);
        ImGui::TableSetupColumn("Recovery & Cheats", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::SeparatorText("Stats & Unlocks");
        if (ImGui::Button("Unlock all items", ImVec2(200, 0))) {
            Port_DebugAction_GiveAllItems();
            Port_DebugMenu_ToastFromExternal("All items granted");
        }
        if (ImGui::Button("All kinstones fused", ImVec2(200, 0))) {
            Port_DebugAction_AllKinstones();
            Port_DebugMenu_ToastFromExternal("All kinstones");
        }
        if (ImGui::Button("All figurines (130)", ImVec2(200, 0))) {
            Port_DebugAction_AllFigurines130();
            Port_DebugMenu_ToastFromExternal("All 130 figurines (no game-clear)");
        }
        if (ImGui::Button("Figurines 100% (marks beaten)", ImVec2(200, 0))) {
            Port_DebugAction_AllFigurines100();
            Port_DebugMenu_ToastFromExternal("136 figurines + game marked cleared");
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::SeparatorText("Recovery & Cheats");
        if (ImGui::Button("Heal", ImVec2(120, 0))) {
            Port_DebugAction_HealFull();
            Port_DebugMenu_ToastFromExternal("Healed");
        }
        ImGui::SameLine();
        if (ImGui::Button("Max hearts", ImVec2(120, 0))) {
            Port_DebugAction_MaxHearts();
            Port_DebugMenu_ToastFromExternal("Hearts maxed");
        }
        if (ImGui::Button("999 rupees", ImVec2(120, 0))) {
            Port_DebugAction_MaxRupees();
            Port_DebugMenu_ToastFromExternal("999 rupees");
        }
        ImGui::SameLine();
        if (ImGui::Button("999 shells", ImVec2(120, 0))) {
            Port_DebugAction_MaxShells();
            Port_DebugMenu_ToastFromExternal("999 shells");
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Per-item toggle");
    DrawRibbonItemToggles();

    ImGui::Spacing();
    ImGui::SeparatorText("Dungeon items (any dungeon)");
    DrawRibbonDungeonItems();

    ImGui::Spacing();
    ImGui::SeparatorText("Counts & capacities");
    DrawRibbonStats();

    ImGui::Spacing();
    ImGui::SeparatorText("Bottle contents");
    DrawRibbonBottles();

    ImGui::Spacing();
    ImGui::SeparatorText("Charm / Picolyte");
    DrawRibbonBuffs();
}
