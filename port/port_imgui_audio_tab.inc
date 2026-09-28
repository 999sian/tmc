/* Audio ribbon tab — extracted from port_imgui_menu.cpp. Included into the same
 * translation unit; relies on the declarations already visible there. */
/* Forward decl pulled from port_audio_mute.h, kept local so this file
 * doesn't depend on the new header for the rest of its surface area. */
extern "C" {
typedef enum {
    AUDIO_MUTE_EZLO_VOICE,
    AUDIO_MUTE_NPC_VOICE,
    AUDIO_MUTE_LOW_HEALTH_BEEP,
    AUDIO_MUTE_COUNT,
} AudioMuteCategory;
bool Port_AudioMute_IsEnabled(AudioMuteCategory c);
void Port_AudioMute_SetEnabled(AudioMuteCategory c, bool on);
const char* Port_AudioMute_Label(AudioMuteCategory c);
const char* Port_AudioMute_Description(AudioMuteCategory c);
}

static void DrawRibbonAudioTab(void) {
    /* Master volume - a basic level control, active in both accurate and
     * enhanced modes (default 100% leaves the mix unchanged). */
    {
        float vol = Port_Audio_GetMasterVolume() * 100.0f;
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::SliderFloat("Master volume", &vol, 0.0f, 100.0f, "%.0f%%")) {
            float v = vol / 100.0f;
            Port_Audio_SetMasterVolume(v);
            Port_Config_SetMasterVolume(v);
        }
        RandoUi_HelpTooltip("Scales the final mixed game audio. 100% = unchanged. Persists "
                            "across launches. Leave at 100% for a faithful level match when "
                            "A/B-testing against hardware in GBA-accurate mode.");
        ImGui::Separator();
    }

    bool gbaAccurate = Port_Audio_IsGbaAccurate();
    if (ImGui::Checkbox("GBA-accurate audio", &gbaAccurate)) {
        Port_Audio_SetGbaAccurate(gbaAccurate);
    }
    RandoUi_HelpTooltip("On: NEAREST resampling (the hardware's no-interpolation "
                        "sample-and-hold 'crunch') and the output is handed straight to "
                        "the device with no post-process DSP - for A/B comparison against "
                        "real hardware / mGBA.\n\n"
                        "Off (default): band-limited BLEP/BLAMP resampling plus the DC-blocker / "
                        "stereo-widen / soft-clip chain tuned for modern speakers.");

    ImGui::Separator();

    /* Enhancement sliders — only meaningful while the enhanced (non-accurate)
       post-process chain is running, so grey them out in GBA-accurate mode. */
    ImGui::BeginDisabled(gbaAccurate);

    if (ImGui::BeginTable("##audio_enhancements", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch);

        // Stereo width
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Stereo width");
        ImGui::TableSetColumnIndex(1);
        float width = Port_Audio_GetWidth();
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::SliderFloat("##width", &width, 1.00f, 1.50f, "%.2f")) {
            Port_Audio_SetWidth(width);
        }
        RandoUi_HelpTooltip("Mid/side stereo widening. 1.00 = mono image (reference), "
                            "1.20 = default. The mid is never altered, so mono playback always "
                            "collapses cleanly to the original mix. Lower values (~1.12) reduce "
                            "hard-panned peak overshoot. No effect in GBA-accurate mode.");

        // Reverb
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Reverb");
        ImGui::TableSetColumnIndex(1);
        int reverb = Port_Audio_GetReverbLevel();
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::SliderInt("##reverb", &reverb, 0, 24)) {
            Port_Audio_SetReverbLevel(reverb);
        }
        RandoUi_HelpTooltip("Adds a short room tail to sampled (PCM) voices - drums, bass, "
                            "some leads - while the chiptune PSG/CGB voices stay dry by the "
                            "synth's mix order, so it adds space without muddying the melody. "
                            "0 = off (default). ~12 is a gentle, musical amount. Applies live "
                            "(does not restart the music). No effect in GBA-accurate mode.");

        ImGui::EndTable();
    }

    ImGui::EndDisabled();
    ImGui::Separator();

    ImGui::TextWrapped("Per-category SFX mutes. Each toggle suppresses "
                       "the matching sound IDs at the SoundReq / EnqueueSFX "
                       "entry points - music and other SFX are untouched.");
    ImGui::Separator();

    if (ImGui::BeginTable("##sfx_mutes", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Left", ImGuiTableColumnFlags_WidthFixed, 220.0f);
        ImGui::TableSetupColumn("Right", ImGuiTableColumnFlags_WidthStretch);

        for (int i = 0; i < (int)AUDIO_MUTE_COUNT; ++i) {
            if ((i % 2) == 0)
                ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(i % 2);

            bool on = Port_AudioMute_IsEnabled((AudioMuteCategory)i);
            const char* label = Port_AudioMute_Label((AudioMuteCategory)i);
            const char* desc = Port_AudioMute_Description((AudioMuteCategory)i);
            if (ImGui::Checkbox(label, &on)) {
                Port_AudioMute_SetEnabled((AudioMuteCategory)i, on);
            }
            if (desc && desc[0]) {
                RandoUi_HelpTooltip(desc);
            }
        }
        ImGui::EndTable();
    }
}
