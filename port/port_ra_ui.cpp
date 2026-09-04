/*
 * port_ra_ui.cpp — RetroAchievements UI: the F8 "Achievements" tab and the
 * unlock notification overlay.
 *
 * Reads *only* port_ra.h (state, lists, toast queue) and the ra_* config
 * accessors — rcheevos itself is never touched from here, so this file has
 * no knowledge of runtime addresses, hashes or HTTP.
 *
 * When TMC_RA is undefined both entry points are empty, so the --ra=n build
 * links with no port_ra.o / rcheevos objects at all.
 */

#include "port_ra_ui.h"

#ifndef TMC_RA

extern "C" void Port_RA_UI_DrawTab(void) {}
extern "C" void Port_RA_UI_DrawOverlay(void) {}

#else

#include <imgui.h>

#include "port_gba_shadow.h" /* diagnostics panel — why a condition reads zero */
#include "port_ra.h"
#include "port_runtime_config.h"

#include <cstdio>
#include <cstring>

/* ---- shared bits ------------------------------------------------------ */

static ImVec4 RA_StateColor(Port_RA_State s) {
    switch (s) {
        case PORT_RA_ACTIVE:
            return ImVec4(0.38f, 0.86f, 0.48f, 1.0f);
        case PORT_RA_ERROR:
            return ImVec4(1.00f, 0.45f, 0.40f, 1.0f);
        case PORT_RA_UNIDENTIFIED:
            return ImVec4(1.00f, 0.80f, 0.35f, 1.0f);
        case PORT_RA_LOGGING_IN:
        case PORT_RA_LOADING_GAME:
            return ImVec4(0.55f, 0.75f, 1.00f, 1.0f);
        case PORT_RA_OFF:
        case PORT_RA_IDLE:
        default:
            return ImVec4(0.68f, 0.68f, 0.68f, 1.0f);
    }
}

/* ---- F8 ribbon tab ---------------------------------------------------- */

static char sUserBuf[64];
static char sPassBuf[128];

static void RA_DrawAccount(Port_RA_State st) {
    const char* signedInAs = Port_RA_GetUsername();
    const bool busy = (st == PORT_RA_LOGGING_IN);

    ImGui::SeparatorText("Account");

    if (signedInAs[0] != '\0') {
        ImGui::Text("Signed in as %s", signedInAs);
        ImGui::SameLine();
        if (ImGui::Button("Log out")) {
            Port_RA_Logout();
            std::memset(sPassBuf, 0, sizeof(sPassBuf));
        }
        return;
    }

    /* Prefill the username from the config once per session — the password
     * is never stored, so it always starts empty. */
    static bool sUserPrefilled = false;
    if (!sUserPrefilled) {
        std::snprintf(sUserBuf, sizeof(sUserBuf), "%s", Port_Config_GetRaUsername());
        sUserPrefilled = true;
    }

    bool submit = false;
    ImGui::BeginDisabled(busy);
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::InputText("Username", sUserBuf, sizeof(sUserBuf), ImGuiInputTextFlags_EnterReturnsTrue))
        submit = true;
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::InputText("Password", sPassBuf, sizeof(sPassBuf),
                         ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue))
        submit = true;
    if (ImGui::Button("Log in"))
        submit = true;
    ImGui::EndDisabled();

    if (busy) {
        ImGui::SameLine();
        ImGui::TextDisabled("(signing in...)");
    }

    if (submit && !busy && sUserBuf[0] != '\0' && sPassBuf[0] != '\0') {
        Port_RA_Login(sUserBuf, sPassBuf);
        std::memset(sPassBuf, 0, sizeof(sPassBuf)); /* keep it out of memory */
    }
    ImGui::TextDisabled("The password is used once to obtain a login token; only the token is saved.");
}

static void RA_DrawStatus(Port_RA_State st) {
    ImGui::SeparatorText("Status");
    ImGui::TextColored(RA_StateColor(st), "%s", Port_RA_GetStatusText());

    const char* title = Port_RA_GetGameTitle();
    ImGui::Text("Game: %s", title[0] ? title : "(not identified)");

    int earned = 0, totalPoints = 0;
    Port_RA_GetPoints(&earned, &totalPoints);
    ImGui::Text("Points: %d / %d", earned, totalPoints);

    int unlocked = 0, totalAch = 0;
    Port_RA_GetProgress(&unlocked, &totalAch);
    ImGui::Text("Unlocked: %d / %d", unlocked, totalAch);
    if (totalAch > 0) {
        char frac[24];
        std::snprintf(frac, sizeof(frac), "%d / %d", unlocked, totalAch);
        ImGui::ProgressBar((float)unlocked / (float)totalAch, ImVec2(-1.0f, 0.0f), frac);
    }

    const char* rp = Port_RA_GetRichPresence();
    if (rp[0]) {
        ImGui::TextDisabled("Rich presence:");
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(rp);
        ImGui::PopTextWrapPos();
    }
}

static void RA_DrawList(void) {
    ImGui::SeparatorText("Achievements");

    const int n = Port_RA_GetAchievementCount();
    if (n <= 0) {
        ImGui::TextDisabled("No achievement set loaded.");
        return;
    }

    if (ImGui::BeginChild("##ra_ach_list", ImVec2(0.0f, 260.0f), ImGuiChildFlags_Borders, 0)) {
        for (int i = 0; i < n; ++i) {
            const Port_RA_Achievement* a = Port_RA_GetAchievement(i);
            if (!a)
                continue;
            ImGui::PushID(i);
            const ImVec4 col =
                a->unlocked ? ImVec4(0.40f, 0.88f, 0.50f, 1.0f) : ImVec4(0.74f, 0.74f, 0.74f, 1.0f);
            ImGui::TextColored(col, "%s %s", a->unlocked ? "[x]" : "[ ]",
                               (a->title && a->title[0]) ? a->title : "(untitled)");
            ImGui::SameLine();
            ImGui::TextDisabled("%d pts", a->points);

            ImGui::Indent(18.0f);
            if (a->description && a->description[0]) {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled("%s", a->description);
                ImGui::PopTextWrapPos();
            }
            /* Measured achievements ("collect 50 of X") report partial
             * progress — show it while the achievement is still locked. */
            if (!a->unlocked && a->measured_percent > 0.0f) {
                char pct[16];
                std::snprintf(pct, sizeof(pct), "%.0f%%", (double)a->measured_percent);
                ImGui::ProgressBar(a->measured_percent / 100.0f,
                                   ImVec2(-1.0f, ImGui::GetTextLineHeight()), pct);
            }
            ImGui::Unindent(18.0f);

            ImGui::PopID();
            if (i + 1 < n)
                ImGui::Separator();
        }
    }
    ImGui::EndChild();
}

/* Why a condition can read zero on a native port: only symbols whose native
 * sizeof matches their retail GBA extent are shadowed. The rest are rejected
 * (64-bit pointer widening moved their fields off the retail offsets) and read
 * as zero, so an achievement keyed on them would be *wrong*, not just stale. */
static void RA_DrawShadowDiagnostics(void) {
    if (!ImGui::CollapsingHeader("Memory shadow (diagnostics)"))
        return;

    int shadowed = 0, rejected = 0;
    uint32_t bytes = 0;
    Port_GbaShadow_GetStats(&shadowed, &rejected, &bytes);
    ImGui::Text("Shadowed: %d symbols, %u bytes/frame", shadowed, (unsigned)bytes);
    /* Rows, not globals: one entry can group several symbols that share a
     * rejected range, so the row count is not a symbol count. */
    ImGui::Text("Rejected: %d rows", rejected);

    if (rejected > 0) {
        ImGui::TextWrapped("Rejected ranges read as zero: their native layout differs from "
                           "retail (64-bit pointers widened the struct), so a condition keyed "
                           "on them would be wrong rather than merely stale.");
        if (ImGui::BeginChild("##ra_shadow_rejected", ImVec2(0.0f, 140.0f), ImGuiChildFlags_Borders, 0)) {
            const char* reason = nullptr;
            for (int i = 0; i < rejected; ++i) {
                const char* name = Port_GbaShadow_GetRejected(i, &reason);
                if (!name)
                    break; /* list ended early — trust the NULL, not the count */
                ImGui::TextUnformatted(name);
                ImGui::SameLine();
                ImGui::TextDisabled("— %s", reason ? reason : "(no reason given)");
            }
        }
        ImGui::EndChild();
    }

    /* The list that actually matters once a set is loaded: every address the
     * achievement logic asked for and the shadow could not serve. Each line is
     * one missing port_gba_shadow.c row. */
    const int unmapped = Port_RA_GetUnmappedCount();
    ImGui::Text("Unserved reads from the loaded set: %d", unmapped);
    if (unmapped > 0) {
        ImGui::TextWrapped("These addresses need a shadow row; the achievements reading them "
                           "stay unsupported until one exists.");
        if (ImGui::BeginChild("##ra_shadow_unmapped", ImVec2(0.0f, 120.0f), ImGuiChildFlags_Borders, 0)) {
            for (int i = 0; i < unmapped; ++i) {
                uint32_t gba = 0;
                const uint32_t flat = Port_RA_GetUnmappedAddress(i, &gba);
                ImGui::Text("RA 0x%06X  ->  GBA 0x%08X", (unsigned)flat, (unsigned)gba);
            }
        }
        ImGui::EndChild();
        const uint32_t dropped = Port_RA_GetUnmappedDropped();
        if (dropped > 0)
            ImGui::TextDisabled("(+%u further misses not recorded)", (unsigned)dropped);
    }

    /* Self-test perturbs a few bytes of live engine state, so it is strictly
     * on demand — never per frame. */
    static const char* sSelfTestResult = nullptr;
    if (ImGui::Button("Run shadow self-test"))
        sSelfTestResult = Port_GbaShadow_SelfTest() ? "Self-test passed: GBA address arithmetic is correct."
                                                    : "Self-test FAILED — see stderr.";
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(360.0f);
        ImGui::TextUnformatted("Writes 5 sentinel bytes into gSave/gRoomControls, forces two "
                               "refreshes, then restores them before returning. The engine and "
                               "the RA frame tick both run on the game thread, so nothing can "
                               "observe the sentinel.");
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(writes and restores 5 bytes of live state)");
    if (sSelfTestResult)
        ImGui::TextUnformatted(sSelfTestResult);
}

extern "C" void Port_RA_UI_DrawTab(void) {
    ImGui::TextWrapped("Unlocks are tracked on retroachievements.org while you play. "
                       "Needs a free RA account and an internet connection.");
    ImGui::Separator();

    bool enabled = Port_Config_GetRaEnabled();
    if (ImGui::Checkbox("Enable RetroAchievements", &enabled))
        Port_Config_SetRaEnabled(enabled);

    const Port_RA_State st = Port_RA_GetState();
    if (!enabled) {
        ImGui::TextDisabled("Disabled — no data is sent to retroachievements.org.");
        return;
    }
    if (st == PORT_RA_OFF) {
        ImGui::SameLine();
        ImGui::TextDisabled("(restart tmc_pc to connect)");
    }

    bool notify = Port_Config_GetRaNotifications();
    if (ImGui::Checkbox("Show unlock notifications", &notify))
        Port_Config_SetRaNotifications(notify);

    RA_DrawAccount(st);
    RA_DrawStatus(st);
    RA_DrawList();
    ImGui::Separator();
    RA_DrawShadowDiagnostics();
}

/* ---- unlock notification overlay -------------------------------------- *
 * Toasts are drained out of port_ra.c's queue into a tiny fixed ring here
 * and drawn bottom-right, newest at the bottom, stacking upwards. Windows
 * are NoInputs so they never steal a click from the game or the F8 menu. */

namespace {
constexpr int kMaxToasts = 5;
constexpr float kToastLife = 4.5f;
constexpr float kFadeIn = 0.15f;
constexpr float kFadeOut = 0.75f;

struct LiveToast {
    Port_RA_Toast toast;
    float age;
    float height; /* measured last frame, for stacking */
};

LiveToast sToasts[kMaxToasts];
int sToastCount = 0;
} // namespace

extern "C" void Port_RA_UI_DrawOverlay(void) {
    const bool notify = Port_Config_GetRaNotifications();

    /* Always drain, even with notifications off, so the producer's queue
     * can never back up. */
    Port_RA_Toast incoming;
    while (Port_RA_PopToast(&incoming)) {
        if (!notify)
            continue;
        if (sToastCount == kMaxToasts) {
            for (int i = 1; i < kMaxToasts; ++i)
                sToasts[i - 1] = sToasts[i];
            sToastCount = kMaxToasts - 1;
        }
        LiveToast& slot = sToasts[sToastCount++];
        slot.toast = incoming;
        slot.age = 0.0f;
        slot.height = ImGui::GetTextLineHeightWithSpacing() * 3.0f; /* guess for frame 1 */
    }

    if (sToastCount == 0)
        return; /* idle: two calls and out */

    const ImGuiIO& io = ImGui::GetIO();
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs;
    const float pad = 12.0f;
    float stackY = pad;
    int keep = 0;

    for (int i = 0; i < sToastCount; ++i) {
        LiveToast live = sToasts[i];
        live.age += io.DeltaTime;
        if (live.age >= kToastLife)
            continue; /* expired — dropped by the compaction below */

        float alpha = 1.0f;
        if (live.age < kFadeIn)
            alpha = live.age / kFadeIn;
        else if (live.age > kToastLife - kFadeOut)
            alpha = (kToastLife - live.age) / kFadeOut;
        if (alpha < 0.0f)
            alpha = 0.0f;
        else if (alpha > 1.0f)
            alpha = 1.0f;

        char id[32];
        std::snprintf(id, sizeof(id), "##ra_toast_%d", i);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        ImGui::SetNextWindowBgAlpha(0.88f);
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - pad, io.DisplaySize.y - stackY),
                                ImGuiCond_Always, ImVec2(1.0f, 1.0f));
        if (ImGui::Begin(id, nullptr, flags)) {
            ImGui::PushTextWrapPos(300.0f);
            ImGui::TextColored(ImVec4(1.0f, 0.86f, 0.35f, 1.0f), "%s", live.toast.title);
            if (live.toast.subtitle[0])
                ImGui::TextUnformatted(live.toast.subtitle);
            ImGui::PopTextWrapPos();
            if (live.toast.points > 0)
                ImGui::TextDisabled("%d points", live.toast.points);
            live.height = ImGui::GetWindowHeight();
        }
        ImGui::End();
        ImGui::PopStyleVar();

        stackY += live.height + 6.0f;
        sToasts[keep++] = live;
    }
    sToastCount = keep;
}

#endif /* TMC_RA */
