/* Randomizer ribbon tab — extracted from port_imgui_menu.cpp. Included into the same
 * translation unit; relies on the declarations already visible there. */
/* Randomizer tab — wraps the native in-process engine at port/rando/.
 * Pressing "Roll" rolls
 * a seed; subsequent item-give intercepts (M1: chest rewards) apply
 * the new permutation immediately. */
extern "C" const char* Port_FindBaseRomPath(void);

static char sRandoSeedBuf[64] = "";    /* empty/0 = engine picks; text is hashed */
static char sRandoResult[192] = { 0 }; /* last roll outcome line */
static bool sRandoResultOk = true;
static std::vector<char> sRandoSpoiler;
static bool sRandoSpoilerHidden = false;    /* race-seed convention: hide until revealed */
static uint64_t sRandoSpoilerHiddenSeed = 0;
static uint64_t sRandoSpoilerHiddenFingerprint = 0;
static ImGuiTextFilter sRandoSpoilerFilter; /* spoiler log line filter */
static std::string sRandoSpoilerExportResult;
static bool sRandoSpoilerExportOk = false;
static RandomizerSettings sRandoUiSettings;
static bool sRandoUiSettingsInit = false;

static void RefreshRandoSpoiler() {
    if (!Rando_IsActive()) {
        sRandoSpoiler.clear();
        sRandoSpoilerHidden = false;
        return;
    }
    if (sRandoSpoilerHidden &&
        (sRandoSpoilerHiddenSeed != Rando_GetSeed64() ||
         sRandoSpoilerHiddenFingerprint != Rando_GetLogicFingerprint()))
        sRandoSpoilerHidden = false;
    const size_t bytes = Rando_GetSpoiler(nullptr, 0);
    sRandoSpoiler.resize(bytes);
    if (bytes != 0)
        Rando_GetSpoiler(sRandoSpoiler.data(), bytes);
}

static void ExportRandoSpoiler() {
    const std::filesystem::path directory =
        std::filesystem::path(Port_Save_GetActivePath()).parent_path() / "spoilers";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        sRandoSpoilerExportResult = error.message();
        sRandoSpoilerExportOk = false;
        return;
    }

    const RandomizerSettings settings = Rando_GetSettings();
    const unsigned long long fingerprint = (unsigned long long)(Rando_IsLogicSeed()
        ? Rando_GetLogicFingerprint() : Rando_SettingsFingerprint(&settings));
    char filename[96];
    std::snprintf(filename, sizeof(filename), "seed-%llu-%016llX.txt",
                  (unsigned long long)Rando_GetSeed64(), fingerprint);
    const std::filesystem::path path = directory / filename;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (out) {
        out.write(sRandoSpoiler.data(), (std::streamsize)(sRandoSpoiler.size() - 1));
        out.close();
    }
    sRandoSpoilerExportOk = (bool)out;
    sRandoSpoilerExportResult = sRandoSpoilerExportOk ? "Saved: " + path.string()
                                                       : "Could not save: " + path.string();
}

/* Shared player-facing strings for rando settings, referenced by BOTH the F8
 * tab and the file-select sidebar so the two entry points can never drift
 * apart on labels/wording (a prior UX bug). */
static const char* const kRandoPoolCombo[RANDO_ITEM_POOL_COUNT] = {
    "Balanced - standard item pool",
    "Reduced - smaller item pool",
    "Plentiful - extra major items",
};
static const char* const kRandoPoolTooltip =
    "Balanced uses the standard pool; Reduced removes "
    "some extra items; Plentiful adds extra major items. Every choice uses "
    "the same reachability check before the seed starts.";
static const char* const kRandoAccessCombo[RANDO_ACCESS_COUNT] = {
    "Goal only (fastest generation)",
    "All non-key checks reachable",
    "All checks reachable",
};
static const char* const kRandoAccessTooltip =
    "Goal only: just the final boss must be reachable (a seed may bury optional "
    "checks behind items you never need). All non-key: every check except "
    "unshuffled small keys must be reachable. All: every check reachable. "
    "Stronger modes reject more seeds during generation but never make a seed "
    "unbeatable.";
static const char* const kRandoTrickOcarina = "Ocarina Glitch - ToD entry without Flippers";
static const char* const kRandoTrickCrenel = "Crenel Clip - Mt. Crenel to Castor Wilds";
static const char* const kRandoTrickPjs = "Portal Jump Storage - early Cloud Tops";
static const char* const kRandoTrickTooltip = "Glitch-logic tier: progression may be placed behind these documented "
                                              "speedrun glitches. Requires Glitchless logic OFF.";

/* ---- Cosmetics (.logic !color settings) ----------------------------------
 * A RANDO_SETTING_COLOR setting carries option_count default color sets
 * (RGB555 hex strings in opt_value[]). The override value consumed by
 * ParseColorDirective is comma-separated RGB555 hex, one per set
 * (e.g. "7C1F,03E0"). Per the `.logic` spec, defaults never set defines:
 * the override only exists once the player actually edits a color, so an
 * enabled-but-untouched setting still rolls vanilla. */
extern "C" void Rando_Cosmetic_Apply(void);                           /* rando_cosmetic.cpp — live palette re-apply */
extern "C" void Rando_SetCosmetics(int tunic_color, int heart_color); /* rando.cpp — live cosmetic settings */

typedef struct RandoColorUiState {
    char define[48];
    bool enabled; /* checkbox; the engine override only exists once dirty */
    bool dirty;   /* an edit was committed at least once this session */
    bool pending; /* floats edited; commit once the picker goes idle */
    float col[RANDO_LOGIC_MAX_COLOR_SETS][3];
} RandoColorUiState;
static RandoColorUiState sRandoColorUi[32];
static int sRandoColorUiCount = 0;

static bool RandoUi_FindOverrideValue(const char* define, const char** out_value) {
    const uint32_t n = RandoLogic_GetOverrideCount();
    for (uint32_t i = 0; i < n; ++i) {
        const char* name = NULL;
        const char* value = NULL;
        if (RandoLogic_GetOverride(i, &name, &value) && name != NULL && std::strcmp(name, define) == 0) {
            if (out_value != NULL)
                *out_value = value;
            return true;
        }
    }
    return false;
}

/* GBA RGB555 layout: R in the low 5 bits (matches ParseColorDirective's
 * packing and the `0x..._0 & 0x1F` eventdefine extraction in .logic). */
static void RandoUi_Rgb555ToFloat(unsigned v, float out[3]) {
    out[0] = (float)(v & 0x1F) / 31.0f;
    out[1] = (float)((v >> 5) & 0x1F) / 31.0f;
    out[2] = (float)((v >> 10) & 0x1F) / 31.0f;
}

static unsigned RandoUi_FloatToRgb555(const float in[3]) {
    unsigned c[3];
    for (int i = 0; i < 3; ++i) {
        float f = in[i];
        if (f < 0.0f)
            f = 0.0f;
        if (f > 1.0f)
            f = 1.0f;
        c[i] = (unsigned)(f * 31.0f + 0.5f);
    }
    return (c[2] << 10) | (c[1] << 5) | c[0];
}

/* Per-define UI cache. Needed because the engine never echoes overrides back
 * into opt_value[] (those always hold the file defaults after a reparse). */
static RandoColorUiState* RandoUi_ColorState(const RandoLogicSetting* s) {
    for (int i = 0; i < sRandoColorUiCount; ++i) {
        if (std::strcmp(sRandoColorUi[i].define, s->define) == 0)
            return &sRandoColorUi[i];
    }
    if (sRandoColorUiCount >= (int)(sizeof(sRandoColorUi) / sizeof(sRandoColorUi[0]))) {
        return NULL;
    }
    RandoColorUiState* st = &sRandoColorUi[sRandoColorUiCount++];
    std::snprintf(st->define, sizeof(st->define), "%s", s->define);
    for (int j = 0; j < RANDO_LOGIC_MAX_COLOR_SETS; ++j) {
        unsigned v = 0x7FFF; /* spec: white when no default given */
        if (j < s->option_count)
            v = (unsigned)std::strtoul(s->opt_value[j], NULL, 16);
        RandoUi_Rgb555ToFloat(v, st->col[j]);
    }
    /* Pre-existing override (sidecar restore / earlier session): adopt it. */
    const char* ov = NULL;
    if (RandoUi_FindOverrideValue(s->define, &ov) && ov != NULL && ov[0] != '\0') {
        st->enabled = true;
        st->dirty = true;
        const char* p = ov;
        int j = 0;
        while (*p != '\0' && j < RANDO_LOGIC_MAX_COLOR_SETS) {
            char* end = NULL;
            unsigned v = (unsigned)std::strtoul(p, &end, 16);
            if (end == p)
                break;
            RandoUi_Rgb555ToFloat(v, st->col[j++]);
            p = end;
            while (*p == ',' || *p == ' ')
                ++p;
        }
    }
    return st;
}

/* Parser overrides are edited only before a seed is active. A live seed uses
 * this parser's locations and keys, so reparsing it would change its awards. */
static void RandoUi_Reparse(void) {
    RandoLogic_Reparse();
    Port_RandoFileMenu_PersistLogicOverrides();
}

static void RandoUi_CommitColorOverride(RandoColorUiState* st, int set_count) {
    if (Rando_IsActive())
        return;
    char value[48]; /* 8 sets x "XXXX," fits; engine caps stored values at 31 */
    size_t len = 0;
    for (int j = 0; j < set_count && j < RANDO_LOGIC_MAX_COLOR_SETS; ++j) {
        len += (size_t)std::snprintf(value + len, sizeof(value) - len, "%s%04X", j ? "," : "",
                                     RandoUi_FloatToRgb555(st->col[j]));
        if (len >= sizeof(value) - 1)
            break;
    }
    if (len > 31) {
        std::fprintf(stderr, "[RANDO] color override %s exceeds engine value cap (%u chars) - truncated\n", st->define,
                     (unsigned)len);
    }
    RandoLogic_SetOverride(st->define, value);
    st->dirty = true;
    RandoUi_Reparse();
    std::fprintf(stderr, "[RANDO] color override %s = %s\n", st->define, value);
}

/* The engine only exposes SetOverride + ClearOverrides-all; an empty-value
 * override is NOT vanilla (ParseColorDirective would still define the bare
 * flag and flip !ifdef blocks). So clearing one define = snapshot the other
 * overrides, ClearOverrides, re-set the survivors, reparse — the selective
 * version of rando_file_menu.c's ClearOverrides+Reparse reset. */
static void RandoUi_RemoveOverride(const char* define) {
    if (Rando_IsActive())
        return;
    static char names[RANDO_LOGIC_MAX_SETTINGS][48];
    static char values[RANDO_LOGIC_MAX_SETTINGS][32]; /* engine value cap */
    const uint32_t n = RandoLogic_GetOverrideCount();
    uint32_t kept = 0;
    for (uint32_t i = 0; i < n && kept < RANDO_LOGIC_MAX_SETTINGS; ++i) {
        const char* name = NULL;
        const char* value = NULL;
        if (!RandoLogic_GetOverride(i, &name, &value) || name == NULL)
            continue;
        if (std::strcmp(name, define) == 0)
            continue;
        std::snprintf(names[kept], sizeof(names[0]), "%s", name);
        std::snprintf(values[kept], sizeof(values[0]), "%s", value ? value : "");
        kept++;
    }
    RandoLogic_ClearOverrides();
    for (uint32_t i = 0; i < kept; ++i)
        RandoLogic_SetOverride(names[i], values[i]);
    RandoUi_Reparse();
    std::fprintf(stderr, "[RANDO] color override %s cleared (vanilla)\n", define);
}

static void DrawRandoCosmeticsSection(void) {
    ImGui::Spacing();
    if (!ImGui::CollapsingHeader("Cosmetics", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    static const char* kTunicColors[] = { "Green (Vanilla)", "Red", "Blue", "Purple", "Orange", "Grey", "Random" };
    static const char* kHeartColors[] = { "Red (Vanilla)", "Blue", "Green", "Yellow", "Purple", "Rainbow", "Random" };

    int tunic = Port_Config_GetRandoTunicColor();
    int heart = Port_Config_GetRandoHeartColor();
    bool changed = false;

    ImGui::SetNextItemWidth(200);
    if (ImGui::Combo("Tunic color", &tunic, kTunicColors, 7)) {
        changed = true;
    }
    ImGui::SetNextItemWidth(200);
    if (ImGui::Combo("Heart color", &heart, kHeartColors, 7)) {
        changed = true;
    }

    if (changed) {
        Port_Config_SetRandoSettings(
            Port_Config_GetRandoGlitchless(), Port_Config_GetRandoObscure(), Port_Config_GetRandoKinstones(),
            Port_Config_GetRandoEntrances(), Port_Config_GetRandoDojos(), Port_Config_GetRandoOpenWorld(),
            Port_Config_GetRandoItemPool(), Port_Config_GetRandoHomewarp(), Port_Config_GetRandoStartSword(),
            Port_Config_GetRandoEarlyCrests(), Port_Config_GetRandoInstantText(), tunic, heart);
        /* Keep the F8 roll-settings struct in sync so a subsequent "Roll new
         * seed" carries the picked colors. */
        sRandoUiSettings.tunic_color = tunic;
        sRandoUiSettings.heart_color = heart;
        /* Cosmetics don't affect placement: push them onto the active seed's
         * settings and re-evaluate the palette so the change is live. */
        Rando_SetCosmetics(tunic, heart);
        if (Rando_IsActive()) {
            Rando_Cosmetic_Apply();
        }
    }
}

/* ---- Logic settings browser (shared by the F8 tab + file-select modal) --
 * The `.logic` file declares per-setting window tab, group, and tooltip
 * text; the browser turns the former flat list into OoTR-style progressive
 * disclosure: collapsing tab sections, group separators, a search filter,
 * per-setting upstream tooltips, modified-from-default markers, and
 * right-click reset. Edits route through the same override+reparse path the
 * engine already uses. Active seeds keep their parsed logic and awards. */

static void RandoUi_ApplyOverride(const char* define, const char* value) {
    if (Rando_IsActive())
        return;
    RandoLogic_SetOverride(define, value);
    RandoUi_Reparse();
}

static bool RandoUi_SettingModified(const RandoLogicSetting* s) {
    switch (s->type) {
        case RANDO_SETTING_FLAG:
            return s->flag_on != s->default_flag;
        case RANDO_SETTING_DROPDOWN:
            return s->option_index != s->default_option;
        case RANDO_SETTING_NUMBER:
            return s->number != s->default_number;
        default:
            return false;
    }
}

static void RandoUi_SettingDefaultValue(const RandoLogicSetting* s, char* out, size_t out_len) {
    switch (s->type) {
        case RANDO_SETTING_FLAG:
            std::snprintf(out, out_len, "%s", s->default_flag ? "true" : "false");
            break;
        case RANDO_SETTING_DROPDOWN:
            std::snprintf(
                out, out_len, "%s",
                (s->default_option >= 0 && s->default_option < s->option_count) ? s->opt_value[s->default_option] : "");
            break;
        case RANDO_SETTING_NUMBER:
            std::snprintf(out, out_len, "%d", s->default_number);
            break;
        default:
            out[0] = '\0';
            break;
    }
}

static void RandoUi_HelpTooltip(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(420.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

static int RandoUi_ModifiedSettingCount(void) {
    int n = 0;
    const uint32_t count = RandoLogic_GetSettingCount();
    for (uint32_t i = 0; i < count; ++i) {
        const RandoLogicSetting* s = RandoLogic_GetSetting(i);
        if (s != NULL && s->type != RANDO_SETTING_COLOR && RandoUi_SettingModified(s))
            ++n;
    }
    return n;
}

/* Reset every non-color setting to its file default. Color overrides are
 * preserved (they live in the Cosmetics section and are orthogonal). */
static void RandoUi_ResetSettingsToDefaults(void) {
    if (Rando_IsActive())
        return;
    const uint32_t count = RandoLogic_GetSettingCount();
    for (uint32_t i = 0; i < count; ++i) {
        const RandoLogicSetting* s = RandoLogic_GetSetting(i);
        if (s == NULL || s->type == RANDO_SETTING_COLOR || !RandoUi_SettingModified(s))
            continue;
        char value[40];
        RandoUi_SettingDefaultValue(s, value, sizeof(value));
        RandoLogic_SetOverride(s->define, value);
    }
    RandoUi_Reparse();
}

/* ---- Presets (OoTR convention: load changes everything except cosmetics).
 * Each preset starts from file defaults, then applies its pairs. */
typedef struct RandoUiPresetPair {
    const char* define;
    const char* value;
} RandoUiPresetPair;
typedef struct RandoUiPreset {
    const char* name;
    const char* desc;
    const RandoUiPresetPair* pairs;
    int count;
} RandoUiPreset;

static const RandoUiPresetPair kRandoPresetStandard[] = {
    { "RUPEEMANIA", "true" },       { "SPECIALPOTS", "true" },
    { "DIGGING", "true" },          { "UNDERWATER", "true" },
    { "GOLDEN_ENEMY", "true" },     { "OPEN_TINGLE", "true" },
    { "OPEN_LIBRARY", "true" },     { "CUCCO_SETTING", "CUCCO_5" },
    { "GORON_SETTING", "GORON_5" }, { "BIGGORON_SETTING", "BIGGORON_NORMAL" },
};
static const RandoUiPresetPair kRandoPresetKeysanity[] = {
    { "RUPEEMANIA", "true" },
    { "SPECIALPOTS", "true" },
    { "DIGGING", "true" },
    { "UNDERWATER", "true" },
    { "GOLDEN_ENEMY", "true" },
    { "OPEN_TINGLE", "true" },
    { "OPEN_LIBRARY", "true" },
    { "CUCCO_SETTING", "CUCCO_5" },
    { "GORON_SETTING", "GORON_5" },
    { "BIGGORON_SETTING", "BIGGORON_NORMAL" },
    { "SMALL_KEYS_SETTING", "SMALL_KEYSANITY" },
    { "BIG_KEYS_SETTING", "BIG_KEYSANITY" },
    { "MAP_SETTING", "MAP_KEYSANITY" },
    { "COMPASS_SETTING", "COMPASS_KEYSANITY" },
};
static const RandoUiPresetPair kRandoPresetOpen[] = {
    { "OPENWORLD", "OPENWORLD_ON" }, { "OPEN_WIND_TRIBE", "true" }, { "OPEN_TINGLE", "true" },
    { "OPEN_LIBRARY", "true" },      { "CRENEL_CREST", "true" },    { "FALLS_CREST", "true" },
    { "CLOUD_CREST", "true" },       { "SWAMP_CREST", "true" },     { "SHF_CREST", "true" },
    { "MINISH_CREST", "true" },
};

static const RandoUiPreset kRandoPresets[] = {
    { "File defaults (Beginner)",
      "Every setting at the .logic file's defaults - chests and hearts "
      "shuffled, progression close to vanilla. Best first seed.",
      NULL, 0 },
    { "Standard shuffle",
      "Adds the common location shuffles on top of the defaults: rupees, "
      "special pots, dig spots, underwater spots, golden enemies, all "
      "cucco rounds, Goron merchant sets, and Biggoron. Library and "
      "Tingle siblings start open.",
      kRandoPresetStandard, (int)(sizeof(kRandoPresetStandard) / sizeof(kRandoPresetStandard[0])) },
    { "Keysanity",
      "Standard shuffle plus dungeon small keys, big keys, maps, and "
      "compasses shuffled anywhere in the world.",
      kRandoPresetKeysanity, (int)(sizeof(kRandoPresetKeysanity) / sizeof(kRandoPresetKeysanity[0])) },
    { "Open world (fast)",
      "World obstacles start open, all wind crests are active, and the "
      "Wind Tribe tower, library, and Tingle siblings are unlocked from "
      "the start. Shorter seeds with less walking.",
      kRandoPresetOpen, (int)(sizeof(kRandoPresetOpen) / sizeof(kRandoPresetOpen[0])) },
};

static void RandoUi_ApplyPreset(int preset_index) {
    if (Rando_IsActive())
        return;
    if (preset_index < 0 || preset_index >= (int)(sizeof(kRandoPresets) / sizeof(kRandoPresets[0])))
        return;
    const RandoUiPreset* p = &kRandoPresets[preset_index];
    /* Start from file defaults so presets are absolute, not additive. */
    const uint32_t count = RandoLogic_GetSettingCount();
    for (uint32_t i = 0; i < count; ++i) {
        const RandoLogicSetting* s = RandoLogic_GetSetting(i);
        if (s == NULL || s->type == RANDO_SETTING_COLOR || !RandoUi_SettingModified(s))
            continue;
        char value[40];
        RandoUi_SettingDefaultValue(s, value, sizeof(value));
        RandoLogic_SetOverride(s->define, value);
    }
    for (int i = 0; i < p->count; ++i)
        RandoLogic_SetOverride(p->pairs[i].define, p->pairs[i].value);
    RandoUi_Reparse();
    std::fprintf(stderr, "[RANDO] preset applied: %s\n", p->name);
}

static void DrawRandoPresetsRow(void) {
    static int sPresetIdx = 0;
    const int preset_count = (int)(sizeof(kRandoPresets) / sizeof(kRandoPresets[0]));
    ImGui::SetNextItemWidth(220);
    if (ImGui::BeginCombo("##rando_preset", kRandoPresets[sPresetIdx].name)) {
        for (int i = 0; i < preset_count; ++i) {
            if (ImGui::Selectable(kRandoPresets[i].name, i == sPresetIdx))
                sPresetIdx = i;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(360.0f);
                ImGui::TextUnformatted(kRandoPresets[i].desc);
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Load preset"))
        RandoUi_ApplyPreset(sPresetIdx);
    RandoUi_HelpTooltip(kRandoPresets[sPresetIdx].desc);
}

static void DrawRandoSettingRow(const RandoLogicSetting* s, int idx) {
    ImGui::PushID(idx);
    const bool modified = RandoUi_SettingModified(s);
    if (modified) {
        /* Modified-from-default marker: color cue plus a non-color glyph so
         * the state never relies on color alone. */
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f), "*");
        ImGui::SameLine(0.0f, 4.0f);
    }
    switch (s->type) {
        case RANDO_SETTING_FLAG: {
            bool v = s->flag_on;
            if (ImGui::Checkbox(s->label, &v))
                RandoUi_ApplyOverride(s->define, v ? "true" : "false");
            break;
        }
        case RANDO_SETTING_DROPDOWN: {
            const int oi = s->option_index;
            const char* preview = (oi >= 0 && oi < s->option_count) ? s->opt_label[oi] : "?";
            ImGui::SetNextItemWidth(200);
            if (ImGui::BeginCombo(s->label, preview)) {
                for (int o = 0; o < s->option_count; ++o) {
                    const bool sel = (o == oi);
                    if (ImGui::Selectable(s->opt_label[o], sel))
                        RandoUi_ApplyOverride(s->define, s->opt_value[o]);
                    if (sel)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            break;
        }
        case RANDO_SETTING_NUMBER: {
            /* Commit on release - every commit reparses the whole .logic file,
             * far too heavy per drag pixel. */
            static int sNumEditIdx = -1;
            static int sNumEditVal = 0;
            int v = (sNumEditIdx == idx) ? sNumEditVal : s->number;
            ImGui::SetNextItemWidth(200);
            if (ImGui::SliderInt(s->label, &v, s->num_min, s->num_max)) {
                sNumEditIdx = idx;
                sNumEditVal = v;
            }
            if (ImGui::IsItemDeactivatedAfterEdit() && sNumEditIdx == idx) {
                char text[32];
                std::snprintf(text, sizeof(text), "%d", sNumEditVal);
                RandoUi_ApplyOverride(s->define, text);
                sNumEditIdx = -1;
            }
            break;
        }
        default:
            break;
    }
    if (ImGui::BeginPopupContextItem("##setting_ctx")) {
        ImGui::TextDisabled("%s", s->define);
        if (ImGui::MenuItem("Reset to default", NULL, false, modified)) {
            char value[40];
            RandoUi_SettingDefaultValue(s, value, sizeof(value));
            RandoUi_ApplyOverride(s->define, value);
        }
        ImGui::EndPopup();
    }
    if (s->tooltip[0])
        RandoUi_HelpTooltip(s->tooltip);
    ImGui::PopID();
}

static void DrawRandoLogicSettingsBrowser(float height) {
    static ImGuiTextFilter sFilter;
    const uint32_t count = RandoLogic_GetSettingCount();

    sFilter.Draw("##rando_settings_filter", 200);
    ImGui::SameLine();
    ImGui::TextDisabled("Search");
    const int modified = RandoUi_ModifiedSettingCount();
    if (modified > 0) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f), "* %d changed", modified);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset all"))
            ImGui::OpenPopup("##rando_reset_all");
        if (ImGui::BeginPopup("##rando_reset_all")) {
            ImGui::TextUnformatted("Reset every setting to the file defaults?");
            if (ImGui::Button("Reset")) {
                RandoUi_ResetSettingsToDefaults();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Keep"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    ImGui::BeginChild("##rando_logic_settings", ImVec2(0, height), ImGuiChildFlags_Borders, 0);
    const bool filtering = sFilter.IsActive();
    char cur_tab[24] = "";
    char cur_group[32] = "";
    bool tab_open = true;
    for (uint32_t i = 0; i < count; ++i) {
        const RandoLogicSetting* s = RandoLogic_GetSetting(i);
        if (s == NULL || s->type == RANDO_SETTING_COLOR)
            continue;
        if (filtering) {
            if (!sFilter.PassFilter(s->label) && !sFilter.PassFilter(s->define) && !sFilter.PassFilter(s->group) &&
                !sFilter.PassFilter(s->tab)) {
                continue;
            }
            /* Flat results with tab > group breadcrumbs between sections. */
            if (std::strcmp(cur_tab, s->tab) != 0 || std::strcmp(cur_group, s->group) != 0) {
                std::snprintf(cur_tab, sizeof(cur_tab), "%s", s->tab);
                std::snprintf(cur_group, sizeof(cur_group), "%s", s->group);
                char crumb[64];
                std::snprintf(crumb, sizeof(crumb), "%s > %s", s->tab, s->group);
                ImGui::SeparatorText(crumb);
            }
        } else {
            if (std::strcmp(cur_tab, s->tab) != 0) {
                std::snprintf(cur_tab, sizeof(cur_tab), "%s", s->tab);
                cur_group[0] = '\0';
                /* Per-section changed badge keeps edits findable when the
                 * section is collapsed. */
                int tab_changed = 0;
                for (uint32_t j = i; j < count; ++j) {
                    const RandoLogicSetting* t = RandoLogic_GetSetting(j);
                    if (t == NULL)
                        continue;
                    if (std::strcmp(t->tab, s->tab) != 0)
                        break; /* tabs are contiguous in file order */
                    if (t->type != RANDO_SETTING_COLOR && RandoUi_SettingModified(t))
                        ++tab_changed;
                }
                char header[64];
                if (tab_changed > 0) {
                    std::snprintf(header, sizeof(header), "%s (* %d changed)###tab_%s", s->tab, tab_changed, s->tab);
                } else {
                    std::snprintf(header, sizeof(header), "%s###tab_%s", s->tab, s->tab);
                }
                tab_open = ImGui::CollapsingHeader(
                    header, (std::strcmp(s->tab, "Main Settings") == 0) ? ImGuiTreeNodeFlags_DefaultOpen : 0);
            }
            if (!tab_open)
                continue;
            if (std::strcmp(cur_group, s->group) != 0) {
                std::snprintf(cur_group, sizeof(cur_group), "%s", s->group);
                ImGui::SeparatorText(s->group);
            }
        }
        DrawRandoSettingRow(s, (int)i);
    }
    ImGui::EndChild();
}

/* ---- Built-in logic-aware Tracker overlay ------------------------------
 * Reads the player's live inventory and progress flags, runs the logic
 * propagation solver in the background, and displays owned items/elements,
 * dungeon key status, and a list of reachable checks grouped by area. */

static uint16_t RandoUi_SmallKeyCount(const char* item) {
    if (std::strncmp(item, "SmallKey.0x", 11) != 0 || std::strlen(item) < 13)
        return 0;
    char area_text[5] = { item[9], item[10], item[11], item[12], '\0' };
    char* end = nullptr;
    unsigned long area = std::strtoul(area_text, &end, 16);
    int dungeon = (int)area - 23;
    if (*end != '\0' || dungeon <= 0 || dungeon >= 16)
        return 0;
    return (uint16_t)Rando_GetDungeonKeyCount((unsigned)dungeon);
}

static bool RandoUi_CheckItemOwned(const char* name) {
    if (name == nullptr || std::strlen(name) < 7)
        return false;
    if (std::strncmp(name, "Items.", 6) != 0)
        return false;
    const char* item = name + 6;

    /* Unique progress items */
    if (std::strcmp(item, "GustJar") == 0)
        return GetInventoryValue(ITEM_GUST_JAR) != 0;
    if (std::strcmp(item, "PacciCane") == 0)
        return GetInventoryValue(ITEM_PACCI_CANE) != 0;
    if (std::strcmp(item, "MoleMitts") == 0)
        return GetInventoryValue(ITEM_MOLE_MITTS) != 0;
    if (std::strcmp(item, "PegasusBoots") == 0)
        return GetInventoryValue(ITEM_PEGASUS_BOOTS) != 0;
    if (std::strcmp(item, "RocsCape") == 0)
        return GetInventoryValue(ITEM_ROCS_CAPE) != 0;
    if (std::strcmp(item, "Ocarina") == 0)
        return GetInventoryValue(ITEM_OCARINA) != 0;
    if (std::strcmp(item, "Lantern") == 0)
        return GetInventoryValue(ITEM_LANTERN_ON) != 0 || GetInventoryValue(ITEM_LANTERN_OFF) != 0;
    if (std::strcmp(item, "Flippers") == 0)
        return GetInventoryValue(ITEM_FLIPPERS) != 0;
    if (std::strcmp(item, "PowerBracelets") == 0)
        return GetInventoryValue(ITEM_POWER_BRACELETS) != 0;
    if (std::strcmp(item, "GripRing") == 0)
        return GetInventoryValue(ITEM_GRIP_RING) != 0;

    /* Progressive items (Sword/Shield/Bow/Boomerang/Bombs) */
    if (std::strcmp(item, "SmithSword") == 0)
        return GetInventoryValue(ITEM_SMITH_SWORD) != 0;
    if (std::strcmp(item, "GreenSword") == 0)
        return GetInventoryValue(ITEM_GREEN_SWORD) != 0;
    if (std::strcmp(item, "RedSword") == 0)
        return GetInventoryValue(ITEM_RED_SWORD) != 0;
    if (std::strcmp(item, "BlueSword") == 0)
        return GetInventoryValue(ITEM_BLUE_SWORD) != 0;
    if (std::strcmp(item, "FourSword") == 0)
        return GetInventoryValue(ITEM_FOURSWORD) != 0;

    if (std::strcmp(item, "Shield") == 0)
        return GetInventoryValue(ITEM_SHIELD) != 0;
    if (std::strcmp(item, "MirrorShield") == 0)
        return GetInventoryValue(ITEM_MIRROR_SHIELD) != 0;

    if (std::strcmp(item, "Bow") == 0)
        return GetInventoryValue(ITEM_BOW) != 0;
    if (std::strcmp(item, "LightArrow") == 0)
        return GetInventoryValue(ITEM_LIGHT_ARROW) != 0;

    if (std::strcmp(item, "Bombs") == 0)
        return GetInventoryValue(ITEM_BOMBS) >= 1;
    if (std::strcmp(item, "RemoteBombs") == 0)
        return GetInventoryValue(ITEM_REMOTE_BOMBS) >= 1;

    if (std::strcmp(item, "Boomerang") == 0)
        return GetInventoryValue(ITEM_BOOMERANG) != 0;
    if (std::strcmp(item, "MagicBoomerang") == 0)
        return GetInventoryValue(ITEM_MAGIC_BOOMERANG) != 0;

    /* Elements */
    if (std::strcmp(item, "EarthElement") == 0)
        return GetInventoryValue(ITEM_EARTH_ELEMENT) != 0;
    if (std::strcmp(item, "FireElement") == 0)
        return GetInventoryValue(ITEM_FIRE_ELEMENT) != 0;
    if (std::strcmp(item, "WaterElement") == 0)
        return GetInventoryValue(ITEM_WATER_ELEMENT) != 0;
    if (std::strcmp(item, "WindElement") == 0)
        return GetInventoryValue(ITEM_WIND_ELEMENT) != 0;

    /* Quest Items */
    if (std::strcmp(item, "GraveyardKey") == 0)
        return GetInventoryValue(ITEM_QST_GRAVEYARD_KEY) != 0;
    if (std::strcmp(item, "LonLonKey") == 0)
        return GetInventoryValue(ITEM_QST_LONLON_KEY) != 0;
    if (std::strcmp(item, "WakeUpMushroom") == 0)
        return GetInventoryValue(ITEM_QST_MUSHROOM) != 0;
    if (std::strcmp(item, "JabberNut") == 0)
        return GetInventoryValue(ITEM_JABBERNUT) != 0;
    if (std::strcmp(item, "CarlovMedal") == 0)
        return GetInventoryValue(ITEM_QST_CARLOV_MEDAL) != 0;

    /* Upstream uses SmallKey.0x18; old tracker rows append a key ordinal. */
    if (std::strncmp(item, "SmallKey.0x", 11) == 0) {
        unsigned held = RandoUi_SmallKeyCount(item);
        size_t len = std::strlen(item);
        if (len == 14 && item[13] >= '0' && item[13] <= '9')
            return held > (unsigned)(item[13] - '0');
        return len == 13 && held > 0;
    }
    if (std::strncmp(item, "BigKey.0x", 9) == 0 && std::strlen(item) >= 11) {
        char hex[3] = { item[9], item[10], '\0' };
        unsigned area = (unsigned)std::strtoul(hex, nullptr, 16);
        int dungeon_idx = (int)area - 23;
        if (dungeon_idx >= 0 && dungeon_idx < 16) {
            return Rando_GetDungeonHasBigKey(dungeon_idx);
        }
    }

    return false;
}

static uint16_t RandoUi_GetItemCount(const char* name) {
    if (name != nullptr && std::strncmp(name, "Items.SmallKey.0x", 17) == 0 && std::strlen(name) == 19)
        return RandoUi_SmallKeyCount(name + 6);
    return RandoUi_CheckItemOwned(name) ? 1 : 0;
}

static bool RandoUi_LocationChecked(uint32_t loc_idx) {
    uint32_t key = RandoLogic_GetLocationKeyAt(loc_idx);
    if (key == UINT32_MAX)
        return false;

    if (key & 0x80000000u) {
        uint32_t group = (key >> 24) & 0x7F;
        uint32_t subkey = (key >> 16) & 0xFF;
        if (group == RANDO_SCRIPTED_KEY_SPECIAL) {
            switch (subkey) {
                case RANDO_SPECIAL_KEY_BELL_HP:
                    return CheckLocalFlagByBank(GetFlagBankOffset(2), 0xd0); /* Hyrule Town local flag 0xd0 */
                case RANDO_SPECIAL_KEY_TINGLE_TROPHY:
                    return GetInventoryValue(ITEM_QST_TINGLE_TROPHY) != 0;
                case RANDO_SPECIAL_KEY_FORTRESS_PRIZE:
                    return GetInventoryValue(ITEM_OCARINA) != 0;
            }
        }
        return false;
    }

    uint32_t area = (key >> 16) & 0xFF;
    uint32_t room = (key >> 8) & 0xFF;
    uint32_t flag_or_chest = key & 0xFF;

    unsigned flag = Rando_GetChestLocalFlag(area, room, flag_or_chest);
    if (flag != 0xFF) {
        unsigned offset = GetFlagBankOffset(area);
        return CheckLocalFlagByBank(offset, flag) != 0;
    } else {
        unsigned offset = GetFlagBankOffset(area);
        return CheckLocalFlagByBank(offset, flag_or_chest) != 0;
    }
}

static bool sShowRandoTracker = false;

/* One tracker grid/element cell: bracketed label, accent-colored when owned,
 * dimmed when not. */
static void TrackerCell(const char* label, bool owned, const ImVec4& color) {
    if (owned)
        ImGui::TextColored(color, "[ %s ]", label);
    else
        ImGui::TextDisabled("[ %s ]", label);
}

static void DrawRandoTrackerOverlay(void) {
    if (!sShowRandoTracker)
        return;

    static bool sReached[RANDO_LOGIC_MAX_LOCATIONS] = {};
    static bool sChecked[RANDO_LOGIC_MAX_LOCATIONS] = {};
    static int sFrameThrottle = 15;
    const uint32_t count = RandoLogic_GetLocationCountRaw();

    if (++sFrameThrottle >= 15) {
        sFrameThrottle = 0;
        const uint16_t* active_table = Rando_GetRandomizedItemTable();
        RandoLogic_EvaluateReachability(active_table, Rando_GetRandomizedItemSubtypeTable(),
                                       Rando_GetLocationCount(), Rando_GetSeed64(), RandoUi_GetItemCount,
                                       sReached, count);
        for (uint32_t i = 0; i < count; ++i) {
            sChecked[i] = RandoUi_LocationChecked(i);
        }
    }

    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Randomizer HUD Tracker", &sShowRandoTracker, ImGuiWindowFlags_NoCollapse)) {
        if (ImGui::BeginTabBar("##tracker_tabs")) {
            if (ImGui::BeginTabItem("Items")) {
                struct TrackerItem {
                    const char* label;
                    const char* sym;
                };
                static const TrackerItem kMainItems[] = {
                    { "Gust Jar", "Items.GustJar" },
                    { "Cane of Pacci", "Items.PacciCane" },
                    { "Mole Mitts", "Items.MoleMitts" },
                    { "Pegasus Boots", "Items.PegasusBoots" },
                    { "Roc's Cape", "Items.RocsCape" },
                    { "Ocarina of Wind", "Items.Ocarina" },
                    { "Lantern", "Items.Lantern" },
                    { "Flippers", "Items.Flippers" },
                    { "Power Bracelets", "Items.PowerBracelets" },
                    { "Grip Ring", "Items.GripRing" },
                };

                ImGui::SeparatorText("Key Items");
                if (ImGui::BeginTable("##tracker_items_grid", 5, ImGuiTableFlags_SizingFixedFit)) {
                    for (int i = 0; i < 10; ++i) {
                        if ((i % 5) == 0)
                            ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(i % 5);
                        TrackerCell(kMainItems[i].label, RandoUi_CheckItemOwned(kMainItems[i].sym),
                                    ImVec4(0.4f, 0.9f, 0.4f, 1.0f));
                    }
                    ImGui::EndTable();
                }

                ImGui::SeparatorText("Progressive Upgrades");
                if (ImGui::BeginTable("##tracker_upgrades", 2, ImGuiTableFlags_SizingFixedFit)) {
                    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("Sword");
                    ImGui::TableSetColumnIndex(1);
                    if (RandoUi_CheckItemOwned("Items.FourSword"))
                        ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.2f, 1.0f), "Four Sword (Infused)");
                    else if (RandoUi_CheckItemOwned("Items.BlueSword"))
                        ImGui::Text("Blue Sword");
                    else if (RandoUi_CheckItemOwned("Items.RedSword"))
                        ImGui::Text("Red Sword");
                    else if (RandoUi_CheckItemOwned("Items.GreenSword"))
                        ImGui::Text("Green Sword");
                    else if (RandoUi_CheckItemOwned("Items.SmithSword"))
                        ImGui::Text("Smith's Sword");
                    else
                        ImGui::TextDisabled("None");

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("Shield");
                    ImGui::TableSetColumnIndex(1);
                    if (RandoUi_CheckItemOwned("Items.MirrorShield"))
                        ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.95f, 1.0f), "Mirror Shield");
                    else if (RandoUi_CheckItemOwned("Items.Shield"))
                        ImGui::Text("Small Shield");
                    else
                        ImGui::TextDisabled("None");

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("Bow");
                    ImGui::TableSetColumnIndex(1);
                    if (RandoUi_CheckItemOwned("Items.LightArrow"))
                        ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.2f, 1.0f), "Light Bow");
                    else if (RandoUi_CheckItemOwned("Items.Bow"))
                        ImGui::Text("Bow");
                    else
                        ImGui::TextDisabled("None");

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("Bombs");
                    ImGui::TableSetColumnIndex(1);
                    if (RandoUi_CheckItemOwned("Items.RemoteBombs"))
                        ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.95f, 1.0f), "Remote Bombs");
                    else if (RandoUi_CheckItemOwned("Items.Bombs"))
                        ImGui::Text("Normal Bombs");
                    else
                        ImGui::TextDisabled("None");

                    ImGui::EndTable();
                }

                ImGui::SeparatorText("Elements");
                if (ImGui::BeginTable("##tracker_elements", 4, ImGuiTableFlags_SizingFixedFit)) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    TrackerCell("Earth Element", RandoUi_CheckItemOwned("Items.EarthElement"),
                                ImVec4(0.4f, 0.9f, 0.4f, 1.0f));
                    ImGui::TableSetColumnIndex(1);
                    TrackerCell("Fire Element", RandoUi_CheckItemOwned("Items.FireElement"),
                                ImVec4(0.9f, 0.4f, 0.4f, 1.0f));
                    ImGui::TableSetColumnIndex(2);
                    TrackerCell("Water Element", RandoUi_CheckItemOwned("Items.WaterElement"),
                                ImVec4(0.4f, 0.6f, 0.9f, 1.0f));
                    ImGui::TableSetColumnIndex(3);
                    TrackerCell("Wind Element", RandoUi_CheckItemOwned("Items.WindElement"),
                                ImVec4(0.95f, 0.8f, 0.2f, 1.0f));

                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Dungeons")) {
                static const struct {
                    const char* name;
                    int idx;
                    const char* elem;
                } kDungeonInfo[] = {
                    { "Deepwood Shrine", 1, "Items.EarthElement" },
                    { "Cave of Flames", 2, "Items.FireElement" },
                    { "Fortress of Winds", 3, "Items.WindElement" },
                    { "Temple of Droplets", 4, "Items.WaterElement" },
                    { "Palace of Winds", 6, "Items.WindElement" },
                    { "Dark Hyrule Castle", 7, "" },
                    { "Royal Crypt", 5, "" },
                };

                if (ImGui::BeginTable("##tracker_dungeons", 5,
                                      ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg |
                                          ImGuiTableFlags_BordersOuter)) {
                    ImGui::TableSetupColumn("Dungeon", ImGuiTableColumnFlags_WidthFixed, 180.0f);
                    ImGui::TableSetupColumn("Element", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Keys", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Big Key", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Boss Clear", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    for (int i = 0; i < 7; ++i) {
                        ImGui::TableNextRow();
                        int idx = kDungeonInfo[i].idx;
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("%s", kDungeonInfo[i].name);

                        ImGui::TableSetColumnIndex(1);
                        if (kDungeonInfo[i].elem[0]) {
                            bool has_el = RandoUi_CheckItemOwned(kDungeonInfo[i].elem);
                            if (has_el)
                                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Yes");
                            else
                                ImGui::TextDisabled("-");
                        } else {
                            ImGui::TextDisabled("N/A");
                        }

                        ImGui::TableSetColumnIndex(2);
                        unsigned keys = Rando_GetDungeonKeyCount(idx);
                        if (keys > 0)
                            ImGui::Text("%u keys", keys);
                        else
                            ImGui::TextDisabled("0");

                        ImGui::TableSetColumnIndex(3);
                        bool has_bk = Rando_GetDungeonHasBigKey(idx);
                        if (has_bk)
                            ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.2f, 1.0f), "Yes");
                        else
                            ImGui::TextDisabled("-");

                        ImGui::TableSetColumnIndex(4);
                        if (idx <= 6) {
                            bool cleared = CheckGlobalFlag(idx);
                            if (cleared)
                                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Defeated");
                            else
                                ImGui::TextDisabled("-");
                        } else {
                            ImGui::TextDisabled("N/A");
                        }
                    }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Locations")) {
                static ImGuiTextFilter sLocFilter;
                sLocFilter.Draw("##loc_filter", 180);
                ImGui::SameLine();
                ImGui::TextDisabled("Filter by area/check name");

                ImGui::BeginChild("##tracker_loc_list", ImVec2(0, 0), ImGuiChildFlags_Borders, 0);
                char cur_area[48] = "";
                bool area_open = false;

                for (uint32_t i = 0; i < count; ++i) {
                    RandoLogicLocationType t = RandoLogic_GetLocationType(i);
                    if (t == RANDO_LOGIC_LOCATION_HELPER)
                        continue;

                    const char* name = RandoLogic_GetLocationName(i);
                    if (name == nullptr || name[0] == '\0')
                        continue;

                    bool checked = sChecked[i];
                    if (checked)
                        continue;

                    bool reached = sReached[i];
                    if (!reached)
                        continue;

                    if (sLocFilter.IsActive() && !sLocFilter.PassFilter(name))
                        continue;

                    char area_name[48] = "Overworld";
                    const char* under = std::strchr(name, '_');
                    if (under != nullptr && (size_t)(under - name) < sizeof(area_name)) {
                        std::memcpy(area_name, name, under - name);
                        area_name[under - name] = '\0';
                    }

                    if (std::strcmp(cur_area, area_name) != 0) {
                        std::snprintf(cur_area, sizeof(cur_area), "%s", area_name);
                        int avail = 0;
                        for (uint32_t j = i; j < count; ++j) {
                            const char* n = RandoLogic_GetLocationName(j);
                            if (n == nullptr || RandoLogic_GetLocationType(j) == RANDO_LOGIC_LOCATION_HELPER)
                                continue;
                            if (sChecked[j] || !sReached[j])
                                continue;
                            if (sLocFilter.IsActive() && !sLocFilter.PassFilter(n))
                                continue;
                            if (std::strncmp(n, cur_area, std::strlen(cur_area)) == 0 &&
                                n[std::strlen(cur_area)] == '_') {
                                ++avail;
                            }
                        }
                        char header[64];
                        std::snprintf(header, sizeof(header), "%s (%d available)###area_%s", cur_area, avail, cur_area);
                        area_open = ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen);
                    }

                    if (area_open) {
                        const char* label = name;
                        if (std::strncmp(label, cur_area, std::strlen(cur_area)) == 0 &&
                            label[std::strlen(cur_area)] == '_') {
                            label += std::strlen(cur_area) + 1;
                        }
                        ImGui::Bullet();
                        ImGui::SameLine();
                        ImGui::TextUnformatted(label);
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Logical check: %s", name);
                        }
                    }
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

static void DrawRibbonRandomizerTab(void) {
    if (!sRandoUiSettingsInit) {
        sRandoUiSettings.glitchless_logic = Port_Config_GetRandoGlitchless();
        sRandoUiSettings.obscure_locations = Port_Config_GetRandoObscure();
        sRandoUiSettings.shuffle_kinstones = Port_Config_GetRandoKinstones();
        sRandoUiSettings.shuffle_entrances = Port_Config_GetRandoEntrances();
        sRandoUiSettings.shuffle_dojos = Port_Config_GetRandoDojos();
        sRandoUiSettings.open_world = Port_Config_GetRandoOpenWorld();
        sRandoUiSettings.item_difficulty = (RandoItemPoolDifficulty)Port_Config_GetRandoItemPool();
        sRandoUiSettings.homewarp = Port_Config_GetRandoHomewarp();
        sRandoUiSettings.start_sword = Port_Config_GetRandoStartSword();
        sRandoUiSettings.early_crests = Port_Config_GetRandoEarlyCrests();
        sRandoUiSettings.instant_text = Port_Config_GetRandoInstantText();
        sRandoUiSettings.tunic_color = Port_Config_GetRandoTunicColor();
        sRandoUiSettings.heart_color = Port_Config_GetRandoHeartColor();
        sRandoUiSettings.tricks = (uint32_t)Port_Config_GetRandoTricks();
        sRandoUiSettings.accessibility = (RandoAccessibility)Port_Config_GetRandoAccessibility();
        sRandoUiSettings.shuffle_dungeon_items = Port_Config_GetRandoDungeonItems();
        sRandoUiSettingsInit = true;
    }

    const char* src_rom = Port_FindBaseRomPath();
    const char* region_label = "(unknown)";
    char region[5] = { 0 };
    if (src_rom) {
        FILE* f = std::fopen(src_rom, "rb");
        if (f) {
            std::fseek(f, 0xAC, SEEK_SET);
            const size_t got = std::fread(region, 1, 4, f);
            std::fclose(f);
            if (got == 4) {
                if (std::strcmp(region, "BZME") == 0)
                    region_label = "USA (BZME)";
                else if (std::strcmp(region, "BZMP") == 0)
                    region_label = "EU (BZMP)";
                else if (std::strcmp(region, "BZMJ") == 0)
                    region_label = "JP (BZMJ) - not supported";
                else
                    region_label = region;
            }
        }
    }

    ImGui::TextUnformatted("Native logic randomizer");
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(360.0f);
        ImGui::TextUnformatted("Rolls rewards from the bundled logic file and "
                               "resolves them in the PC game. Generation verifies the chosen "
                               "reachability goal. The seed persists per save slot.");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::Separator();

    ImGui::Text("Source ROM:  %s", src_rom ? src_rom : "(none)");
    ImGui::Text("Region:      %s", region_label);
    ImGui::Text("Logic:       default.logic (%u parsed locations)",
                RandoLogic_GetLocationCountRaw());

    if (Rando_IsActive()) {
        static const char* kPoolNames[RANDO_ITEM_POOL_COUNT] = { "Balanced", "Reduced", "Plentiful" };
        const RandomizerSettings active = Rando_GetSettings();
        const int pool = (active.item_difficulty < RANDO_ITEM_POOL_COUNT) ? (int)active.item_difficulty : 0;
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f), "Active seed: %llu - %s pool%s",
                           (unsigned long long)Rando_GetSeed64(), kPoolNames[pool],
                           active.glitchless_logic ? ", glitchless" : "");
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy seed")) {
            char text[32];
            std::snprintf(text, sizeof(text), "%llu", (unsigned long long)Rando_GetSeed64());
            ImGui::SetClipboardText(text);
        }
        char fp[20];
        if (Rando_IsLogicSeed())
            std::snprintf(fp, sizeof(fp), "%016llX", (unsigned long long)Rando_GetLogicFingerprint());
        else
            std::snprintf(fp, sizeof(fp), "%08X", Rando_SettingsFingerprint(&active));
        ImGui::Text("Logic fingerprint: %s", fp);
        RandoUi_HelpTooltip("Share the seed and logic fingerprint to reproduce placements "
                            "with the same Picori version.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy fingerprint")) {
            ImGui::SetClipboardText(fp);
        }
        ImGui::Checkbox("Show HUD Tracker", &sShowRandoTracker);
    } else {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No seed rolled - vanilla.");
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Settings");

    int difficulty = (int)sRandoUiSettings.item_difficulty;
    bool changed = false;

    ImGui::SetNextItemWidth(280);
    if (ImGui::Combo("Item pool", &difficulty, kRandoPoolCombo, RANDO_ITEM_POOL_COUNT)) {
        sRandoUiSettings.item_difficulty = (RandoItemPoolDifficulty)difficulty;
        changed = true;
    }
    RandoUi_HelpTooltip(kRandoPoolTooltip);

    if (ImGui::Checkbox("Glitchless logic", &sRandoUiSettings.glitchless_logic))
        changed = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Obscure spots", &sRandoUiSettings.obscure_locations))
        changed = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Shuffle kinstones", &sRandoUiSettings.shuffle_kinstones))
        changed = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Shuffle entrances", &sRandoUiSettings.shuffle_entrances))
        changed = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Shuffle dojos", &sRandoUiSettings.shuffle_dojos))
        changed = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Shuffle dungeon items", &sRandoUiSettings.shuffle_dungeon_items))
        changed = true;
    RandoUi_HelpTooltip("Off: keys, maps, compasses and big keys shuffle within their own "
                        "dungeons. On: all four families can appear anywhere (keysanity). "
                        "Each pickup credits its home dungeon.");

    if (ImGui::Checkbox("Open world", &sRandoUiSettings.open_world))
        changed = true;
    RandoUi_HelpTooltip("Starts with every permanently solvable obstacle pre-solved: cut "
                        "trees, cracked blocks, bomb walls, boulder shortcuts, non-key "
                        "doors, bean vines, switches, levers, chest spawns, and "
                        "extendable bridges (1:1 with the GBA randomizer's World "
                        "Settings \"Open\"). Less walking, shorter seeds.");

    ImGui::SameLine();
    if (ImGui::Checkbox("Sleep warp (homewarp)", &sRandoUiSettings.homewarp))
        changed = true;

    if (ImGui::Checkbox("Start with Smith's Sword", &sRandoUiSettings.start_sword))
        changed = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Early Wind Crests", &sRandoUiSettings.early_crests))
        changed = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Fast text (instant text)", &sRandoUiSettings.instant_text))
        changed = true;

    int access = (int)sRandoUiSettings.accessibility;
    ImGui::SetNextItemWidth(280);
    if (ImGui::Combo("Accessibility", &access, kRandoAccessCombo, RANDO_ACCESS_COUNT)) {
        sRandoUiSettings.accessibility = (RandoAccessibility)access;
        changed = true;
    }
    RandoUi_HelpTooltip(kRandoAccessTooltip);

    if (!sRandoUiSettings.glitchless_logic) {
        ImGui::SeparatorText("Glitch tricks (progression may be placed behind these)");
        if (ImGui::CheckboxFlags(kRandoTrickOcarina, &sRandoUiSettings.tricks, RANDO_TRICK_OCARINA_GLITCH))
            changed = true;
        if (ImGui::CheckboxFlags(kRandoTrickCrenel, &sRandoUiSettings.tricks, RANDO_TRICK_CRENEL_CLIP))
            changed = true;
        if (ImGui::CheckboxFlags(kRandoTrickPjs, &sRandoUiSettings.tricks, RANDO_TRICK_PORTAL_JUMP_STORAGE))
            changed = true;
    } else {
        ImGui::TextDisabled("Glitch tricks are selectable when Glitchless logic is OFF.");
    }

    if (changed) {
        Port_Config_SetRandoSettings(
            sRandoUiSettings.glitchless_logic, sRandoUiSettings.obscure_locations, sRandoUiSettings.shuffle_kinstones,
            sRandoUiSettings.shuffle_entrances, sRandoUiSettings.shuffle_dojos, sRandoUiSettings.open_world,
            (int)sRandoUiSettings.item_difficulty, sRandoUiSettings.homewarp, sRandoUiSettings.start_sword,
            sRandoUiSettings.early_crests, sRandoUiSettings.instant_text, sRandoUiSettings.tunic_color,
            sRandoUiSettings.heart_color);
        Port_Config_SetRandoTricks((int)sRandoUiSettings.tricks);
        Port_Config_SetRandoAccessibility((int)sRandoUiSettings.accessibility);
        Port_Config_SetRandoDungeonItems(sRandoUiSettings.shuffle_dungeon_items);
    }

    DrawRandoCosmeticsSection();

    ImGui::Spacing();
    ImGui::SetNextItemWidth(280);
    ImGui::InputText("Seed (empty = random)", sRandoSeedBuf, sizeof(sRandoSeedBuf));
    RandoUi_HelpTooltip("Decimal numbers are used as-is; any other text is hashed to a "
                        "64-bit seed, so phrases work and are shareable.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Random")) {
        uint64_t r = (uint64_t)ImGui::GetTime() * 0x9E3779B97F4A7C15ull ^ Rando_GetSeed64();
        r ^= r >> 30;
        r *= 0xBF58476D1CE4E5B9ull;
        r ^= r >> 27;
        std::snprintf(sRandoSeedBuf, sizeof(sRandoSeedBuf), "%llu", (unsigned long long)r);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy")) {
        ImGui::SetClipboardText(sRandoSeedBuf);
    }

    ImGui::Spacing();
    const bool rollInGameplay = Rando_IsInGameplay();
    const bool rollInFileSelect = !rollInGameplay && Rando_IsInFileSelect();
    ImGui::BeginDisabled(rollInGameplay || rollInFileSelect);
    const bool rolled_normal = ImGui::Button("Roll new seed", ImVec2(150, 0));
    ImGui::SameLine();
    const bool rolled_race = ImGui::Button("Roll race seed", ImVec2(150, 0));
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(360.0f);
        ImGui::TextUnformatted("Rolls a fresh random seed and keeps the spoiler log hidden, "
                               "following the usual race convention. Share the seed number with "
                               "the other racers.");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset to vanilla", ImVec2(140, 0))) {
        Rando_Reset();
        sRandoResult[0] = '\0';
        sRandoSpoiler.clear();
        sRandoSpoilerHidden = false;
        sRandoSpoilerExportResult.clear();
    }
    ImGui::EndDisabled();
    if (rollInGameplay) {
        ImGui::SameLine();
        ImGui::TextDisabled("(locked during gameplay)");
    } else if (rollInFileSelect) {
        ImGui::TextDisabled("On the file screen, roll from the L sidebar - it binds the seed to "
                            "the new save slot. A roll here would be discarded.");
    }
    if (rolled_normal || rolled_race) {
        if (rolled_race)
            sRandoSeedBuf[0] = '\0';
        const uint64_t requested = sRandoSeedBuf[0] ? Rando_SeedFromString(sRandoSeedBuf) : 0;
        uint64_t chosen = 0;
        const RandoStatus status = Rando_GenerateSeed(requested, &sRandoUiSettings, &chosen);
        sRandoResultOk = (status == RANDO_OK);
        switch (status) {
            case RANDO_OK:
                std::snprintf(sRandoResult, sizeof(sRandoResult), "Rolled seed %llu - verified beatable.%s",
                              (unsigned long long)chosen, rolled_race ? " Spoiler log hidden (race)." : "");
                std::snprintf(sRandoSeedBuf, sizeof(sRandoSeedBuf), "%llu", (unsigned long long)chosen);
                sRandoSpoilerHidden = rolled_race;
                sRandoSpoilerHiddenSeed = chosen;
                sRandoSpoilerHiddenFingerprint = Rando_GetLogicFingerprint();
                sRandoSpoilerExportResult.clear();
                break;
            case RANDO_UNBEATABLE:
                std::snprintf(sRandoResult, sizeof(sRandoResult),
                              "No beatable arrangement found for this seed/settings (32 attempts). "
                              "No seed is active.");
                break;
            case RANDO_BAD_SETTINGS:
                std::snprintf(sRandoResult, sizeof(sRandoResult),
                              "Rejected: logic file unavailable or unsupported native settings.");
                break;
            default:
                std::snprintf(sRandoResult, sizeof(sRandoResult),
                              "Generation failed (internal error) - see stderr log.");
                break;
        }
    }

    if (sRandoResult[0]) {
        ImGui::Spacing();
        if (sRandoResultOk) {
            ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f), "%s", sRandoResult);
        } else {
            ImGui::TextColored(ImVec4(0.9f, 0.45f, 0.3f, 1.0f), "%s", sRandoResult);
        }
    }

    RefreshRandoSpoiler();
    if (Rando_IsActive() && !sRandoSpoiler.empty() && sRandoSpoiler[0]) {
        ImGui::Spacing();
        if (sRandoSpoilerHidden) {
            ImGui::TextDisabled("Spoiler log hidden (race seed).");
            ImGui::SameLine();
            if (ImGui::SmallButton("Reveal anyway"))
                sRandoSpoilerHidden = false;
        } else if (ImGui::CollapsingHeader("Spoiler log")) {
            if (ImGui::SmallButton("Copy to clipboard")) {
                ImGui::SetClipboardText(sRandoSpoiler.data());
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Save .txt"))
                ExportRandoSpoiler();
            ImGui::SameLine();
            sRandoSpoilerFilter.Draw("##spoiler_filter", 180);
            ImGui::SameLine();
            ImGui::TextDisabled("Filter");
            if (!sRandoSpoilerExportResult.empty()) {
                ImGui::TextColored(sRandoSpoilerExportOk ? ImVec4(0.4f, 0.85f, 0.4f, 1.0f)
                                                        : ImVec4(0.9f, 0.45f, 0.3f, 1.0f),
                                   "%s", sRandoSpoilerExportResult.c_str());
            }
            ImGui::BeginChild("##rando_spoiler", ImVec2(0, 180), ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_HorizontalScrollbar);
            if (sRandoSpoilerFilter.IsActive()) {
                const char* p = sRandoSpoiler.data();
                while (*p) {
                    const char* nl = std::strchr(p, '\n');
                    const size_t len = nl ? (size_t)(nl - p) : std::strlen(p);
                    char line[512];
                    const size_t copy = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
                    std::memcpy(line, p, copy);
                    line[copy] = '\0';
                    if (sRandoSpoilerFilter.PassFilter(line))
                        ImGui::TextUnformatted(line);
                    p += len + (nl ? 1 : 0);
                }
            } else {
                ImGui::TextUnformatted(sRandoSpoiler.data());
            }
            ImGui::EndChild();
        }
    }
}
