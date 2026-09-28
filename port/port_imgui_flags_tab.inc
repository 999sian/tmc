/* Flags ribbon tab — extracted from port_imgui_menu.cpp. Included into the same
 * translation unit; relies on the declarations already visible there. */
/* Raw flag browser tab (wishlist #5). Pick a bank, scroll the flag grid,
 * toggle bits. The list is clipped so bank 12 (1408 flags) stays cheap. */
static void DrawRibbonFlagsTab(void) {
    static int sBank = 0;
    const int nBanks = Port_DebugQuery_FlagBankCount();
    if (sBank < 0 || sBank >= nBanks)
        sBank = 0;
    const int cur = Port_DebugQuery_CurrentFlagBank();

    /* Flag notifications toggle — persisted to config.json, default off. */
    {
        bool notif = Port_Config_GetDebugFlagNotifications();
        if (ImGui::Checkbox("Flag notifications (log + on-screen toast on flag set)", &notif))
            Port_Config_SetDebugFlagNotifications(notif);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("When enabled, every flag activation is printed to the terminal\nand shown as an on-screen toast notification.");
    }
    ImGui::Separator();

    ImGui::TextUnformatted("Raw save flags (gSave.flags). Bank 0 = global; 1-12 = local pools.");
    ImGui::SetNextItemWidth(220);
    if (ImGui::BeginCombo("Bank", Port_DebugQuery_FlagBankName(sBank))) {
        for (int b = 0; b < nBanks; ++b) {
            const bool selected = (b == sBank);
            ImGui::PushID(b);
            char lbl[64];
            snprintf(lbl, sizeof(lbl), "%s%s", Port_DebugQuery_FlagBankName(b), (b == cur) ? "  (current area)" : "");
            if (ImGui::Selectable(lbl, selected))
                sBank = b;
            if (selected)
                ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (cur >= 0) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Jump to current area"))
            sBank = cur;
    }

    const int size = Port_DebugQuery_FlagBankSize(sBank);
    const unsigned int off = Port_DebugQuery_FlagBankOffset(sBank);
    ImGui::Text("%d flags  (bit offset 0x%03X)", size, off);
    ImGui::TextDisabled("Heads-up: some flags fire cutscenes / credits the moment they're set.");

    /* Search bar + "show only active" checkbox on the same row. */
    static char sSearchBuf[128] = "";
    static bool sShowOnlyActive = false;
    ImGui::SetNextItemWidth(280);
    ImGui::InputText("##search_filter", sSearchBuf, sizeof(sSearchBuf));
    ImGui::SameLine();
    if (sSearchBuf[0] != '\0') {
        if (ImGui::Button("Clear"))
            sSearchBuf[0] = '\0';
        ImGui::SameLine();
    }
    ImGui::TextUnformatted("Search");
    ImGui::SameLine();
    ImGui::Checkbox("Show only active", &sShowOnlyActive);

    /* Candidate list, rebuilt only when the search text / bank change. "Show
     * only active" rescans every frame (membership depends on live flag bits
     * the engine flips behind our back) but reuses the static storage. */
    struct FlagMatch { int bank; int index; };
    static std::vector<FlagMatch> candidates;
    static char sLastSearch[sizeof(sSearchBuf)] = { 1 }; /* never equals "" */
    static bool sLastOnlyActive = false;
    static int sLastBank = -1;

    bool hasSearch = (sSearchBuf[0] != '\0');
    if (sShowOnlyActive || strcmp(sLastSearch, sSearchBuf) != 0 || sLastOnlyActive != sShowOnlyActive ||
        sLastBank != sBank) {
        strcpy(sLastSearch, sSearchBuf);
        sLastOnlyActive = sShowOnlyActive;
        sLastBank = sBank;
        candidates.clear();

        /* Case-insensitive substring test over const char*, no copies. */
        auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; };
        auto contains = [&](const char* hay, const char* needle) {
            if (!hay)
                return false;
            for (; *hay; ++hay) {
                const char* h = hay;
                const char* n = needle;
                while (*h && *n && lower(*h) == lower(*n)) {
                    ++h;
                    ++n;
                }
                if (!*n)
                    return true;
            }
            return false;
        };

        if (hasSearch) {
            /* Text search across all banks. */
            int nb = Port_DebugQuery_FlagBankCount();
            for (int b = 0; b < nb; ++b) {
                int bankSize = Port_DebugQuery_FlagBankSize(b);
                for (int i = 0; i < bankSize; ++i) {
                    if (sShowOnlyActive && Port_DebugQuery_Flag(b, i) == 0)
                        continue;
                    if (contains(Port_DebugQuery_FlagName(b, i), sSearchBuf) ||
                        contains(Port_DebugQuery_FlagDesc(b, i), sSearchBuf))
                        candidates.push_back({b, i});
                }
            }
        } else {
            /* No text search — enumerate the current bank. */
            for (int i = 0; i < size; ++i) {
                if (sShowOnlyActive && Port_DebugQuery_Flag(sBank, i) == 0)
                    continue;
                candidates.push_back({sBank, i});
            }
        }
    }

    ImGui::BeginChild("##flag_list", ImVec2(0, 300), true);

    ImGuiListClipper clipper;
    clipper.Begin((int)candidates.size());
    while (clipper.Step()) {
        for (int d = clipper.DisplayStart; d < clipper.DisplayEnd; ++d) {
            int bank  = candidates[d].bank;
            int i     = candidates[d].index;
            unsigned int bankOff = Port_DebugQuery_FlagBankOffset(bank);
            bool on   = Port_DebugQuery_Flag(bank, i) != 0;

            ImGui::PushID(bank * 100000 + i);

            const char* flagName = Port_DebugQuery_FlagName(bank, i);
            const char* flagDesc = Port_DebugQuery_FlagDesc(bank, i);
            const char* bankName = Port_DebugQuery_FlagBankName(bank);
            char lbl[128];

            bool isKnown = flagName && strcmp(flagName, "UNKNOWN") != 0 && strcmp(flagName, "BEGIN") != 0 && strcmp(flagName, "END") != 0 &&
                strcmp(flagName, "BEGIN_1") != 0 && strcmp(flagName, "END_1") != 0 &&
                strcmp(flagName, "BEGIN_2") != 0 && strcmp(flagName, "END_2") != 0 &&
                strcmp(flagName, "BEGIN_3") != 0 && strcmp(flagName, "END_3") != 0 &&
                strcmp(flagName, "BEGIN_4") != 0 && strcmp(flagName, "END_4") != 0 &&
                strcmp(flagName, "BEGIN_5") != 0 && strcmp(flagName, "END_5") != 0 &&
                strcmp(flagName, "BEGIN_6") != 0 && strcmp(flagName, "END_6") != 0 &&
                strcmp(flagName, "BEGIN_7") != 0 && strcmp(flagName, "END_7") != 0 &&
                strcmp(flagName, "BEGIN_8") != 0 && strcmp(flagName, "END_8") != 0 &&
                strcmp(flagName, "BEGIN_9") != 0 && strcmp(flagName, "END_9") != 0 &&
                strcmp(flagName, "BEGIN_10") != 0 && strcmp(flagName, "END_10") != 0 &&
                strcmp(flagName, "BEGIN_11") != 0 && strcmp(flagName, "END_11") != 0 &&
                strcmp(flagName, "BEGIN_12") != 0 && strcmp(flagName, "END_12") != 0;

            if (hasSearch) {
                if (isKnown)
                    snprintf(lbl, sizeof(lbl), "[%s] idx %4d: %s", bankName, i, flagName);
                else
                    snprintf(lbl, sizeof(lbl), "[%s] idx %4d (0x%03X)   bit 0x%03X", bankName, i, i, bankOff + (unsigned)i);
            } else {
                if (isKnown)
                    snprintf(lbl, sizeof(lbl), "idx %4d (0x%03X): %s", i, i, flagName);
                else
                    snprintf(lbl, sizeof(lbl), "idx %4d (0x%03X)   bit 0x%03X", i, i, bankOff + (unsigned)i);
            }

            if (ImGui::Checkbox(lbl, &on))
                Port_DebugAction_SetFlag(bank, i, on ? 1 : 0);

            if (flagDesc && flagDesc[0] != '\0' && strcmp(flagDesc, "undocumented") != 0) {
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", flagDesc);
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}
