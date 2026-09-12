/* F-Zero controls within the pinned launcher's existing ImGui theme.
 * Included by the build-local backend adapter after its layout helpers. */

void fzero_launcher_source(LauncherModel* m, int p) {
    const char* names[] = {"None", "Keyboard", "Gamepad"};
    for (int source = 0; source < 3; ++source)
        if (ImGui::Selectable(names[source], m->s.player_src[p] == source))
            launcher_model_set_source(m, p, source, 0, nullptr, nullptr);
}

void fzero_launcher_toggle(const char* label, const char* id, int* value,
                           const LauncherTheme& th, float column) {
    row_label(label, th, column);
    bool on = *value != 0;
    if (ImGui::Checkbox(id, &on)) *value = on;
}

void fzero_launcher_display(LauncherModel* m, const LauncherTheme& th) {
    eyebrow("DISPLAY");
    const float column = ImGui::CalcTextSize("Stretch to Fill").x + px(18);
    auto* extra = FZeroLauncherExtraSettings();
    fzero_launcher_toggle("Fullscreen", "##fullscreen", &m->s.fullscreen, th, column);
    fzero_launcher_toggle("Widescreen", "##widescreen", &m->s.widescreen, th, column);
    row_label("Window Scale", th, column);
    ImGui::SetNextItemWidth(px(180));
    char scale[16]; std::snprintf(scale, sizeof(scale), "%dx", m->s.window_scale);
    if (ImGui::BeginCombo("##scale", scale)) {
        for (int value = 1; value <= 8; ++value) {
            std::snprintf(scale, sizeof(scale), "%dx", value);
            if (ImGui::Selectable(scale, m->s.window_scale == value)) m->s.window_scale = value;
        }
        ImGui::EndCombo();
    }
    fzero_launcher_toggle("Stretch to Fill", "##stretch", &m->s.ignore_aspect, th, column);
    row_label("Race Filter", th, column);
    static const char* styles[] = {"Original", "Enhanced", "Vivid", "Black & White"};
    ImGui::SetNextItemWidth(px(180));
    if (ImGui::BeginCombo("##race_filter", styles[extra->visual_style])) {
        for (int value = 0; value < 4; ++value)
            if (ImGui::Selectable(styles[value], extra->visual_style == value)) extra->visual_style = value;
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Original, Enhanced, Vivid or Black & White");
    fzero_launcher_toggle("Linear Filter", "##linear", &m->s.linear_filter, th, column);
    fzero_launcher_toggle("FPS Readout", "##fps", &extra->show_fps, th, column);
}

void fzero_launcher_hotkeys(LauncherModel* m, const LauncherTheme& th) {
    eyebrow("HOTKEYS");
    if (ImGui::BeginTable("fzero_hotkeys", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 1);
        ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthStretch, 2);
        const char* labels[] = {"Settings / Resume", "Screenshot", "FPS Readout", "Quit Game"};
        const char* keys[] = {"F1", "F12", nullptr, "Esc (with settings closed)"};
        for (int row = 0; row < 4; ++row) {
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(col(th.text_muted), "%s", labels[row]);
            ImGui::TableNextColumn();
            if (row == 2) {
                const bool capture = m->hk_capturing && m->capture_hk == LNG_HK_DISPLAY_PERF;
                const char* binding = capture ? "[ press... ]" :
                    m->hotkeys[LNG_HK_DISPLAY_PERF][0] ? m->hotkeys[LNG_HK_DISPLAY_PERF] : "(unbound)";
                ImGui::PushID("fps_binding");
                if (ImGui::Button(binding, ImVec2(px(150), 0)))
                    launcher_model_begin_hk_capture(m, LNG_HK_DISPLAY_PERF);
                ImGui::PopID();
            } else {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(keys[row]);
            }
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::TextWrapped("Gamepad: Select + Start opens settings. Screenshots can also be taken from System in the in-game settings.");
    ImGui::TextWrapped("F12 saves the game image to the screenshots folder while playing or paused. On Mac media keyboards, use Fn + F12.");
}
