#include "ui.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>

#include "app.hpp"
#include "config.hpp"

UIRenderer::UIRenderer(const Config& cfg) {
    std::snprintf(server_url_buf_, sizeof(server_url_buf_), "%s", cfg.server_url().c_str());
}

void UIRenderer::apply_discord_theme() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 3.0f;
    style.ScrollbarRounding = 4.0f;
    style.FramePadding = ImVec2(8, 4);
    style.ItemSpacing = ImVec2(8, 6);
    style.WindowPadding = ImVec2(12, 12);

    auto* c = style.Colors;
    c[ImGuiCol_WindowBg]           = {0.18f, 0.19f, 0.21f, 1.00f};
    c[ImGuiCol_ChildBg]            = {0.18f, 0.19f, 0.21f, 1.00f};
    c[ImGuiCol_PopupBg]            = {0.15f, 0.16f, 0.18f, 1.00f};
    c[ImGuiCol_Border]             = {0.12f, 0.13f, 0.14f, 1.00f};
    c[ImGuiCol_FrameBg]            = {0.25f, 0.27f, 0.30f, 1.00f};
    c[ImGuiCol_FrameBgHovered]     = {0.30f, 0.32f, 0.35f, 1.00f};
    c[ImGuiCol_FrameBgActive]      = {0.35f, 0.37f, 0.40f, 1.00f};
    c[ImGuiCol_TitleBg]            = {0.15f, 0.16f, 0.17f, 1.00f};
    c[ImGuiCol_TitleBgActive]      = {0.18f, 0.19f, 0.21f, 1.00f};
    c[ImGuiCol_ScrollbarBg]        = {0.15f, 0.16f, 0.17f, 1.00f};
    c[ImGuiCol_ScrollbarGrab]      = {0.28f, 0.30f, 0.33f, 1.00f};
    c[ImGuiCol_ScrollbarGrabHovered]= {0.33f, 0.35f, 0.38f, 1.00f};
    c[ImGuiCol_CheckMark]          = {0.34f, 0.54f, 0.93f, 1.00f};
    c[ImGuiCol_SliderGrab]         = {0.34f, 0.54f, 0.93f, 1.00f};
    c[ImGuiCol_SliderGrabActive]   = {0.44f, 0.64f, 1.00f, 1.00f};
    c[ImGuiCol_Button]             = {0.34f, 0.54f, 0.93f, 1.00f};
    c[ImGuiCol_ButtonHovered]      = {0.40f, 0.60f, 1.00f, 1.00f};
    c[ImGuiCol_ButtonActive]       = {0.28f, 0.48f, 0.88f, 1.00f};
    c[ImGuiCol_Header]             = {0.25f, 0.27f, 0.30f, 1.00f};
    c[ImGuiCol_HeaderHovered]      = {0.30f, 0.32f, 0.35f, 1.00f};
    c[ImGuiCol_HeaderActive]       = {0.35f, 0.37f, 0.40f, 1.00f};
    c[ImGuiCol_Separator]          = {0.12f, 0.13f, 0.14f, 1.00f};
    c[ImGuiCol_Text]               = {0.90f, 0.91f, 0.92f, 1.00f};
    c[ImGuiCol_TextDisabled]       = {0.50f, 0.51f, 0.52f, 1.00f};
}

// ---------------------------------------------------------------------------

void UIRenderer::render(App& app) {
    app.update();

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);

    ImGuiWindowFlags wf =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("##Main", nullptr, wf);

    float sidebar_w = 240.0f;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.16f, 0.17f, 0.19f, 1.00f));
    ImGui::BeginChild("##Sidebar", ImVec2(sidebar_w, 0));
    render_sidebar(app);
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::SameLine();

    ImGui::BeginChild("##Content", ImVec2(0, 0));
    render_content(app);
    ImGui::EndChild();

    render_share_popup(app);

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Sidebar
// ---------------------------------------------------------------------------

void UIRenderer::render_sidebar(App& app) {
    ImGui::TextColored({0.34f, 0.54f, 0.93f, 1.0f}, "DRISCORD");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Server");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##server", server_url_buf_, sizeof(server_url_buf_));
    ImGui::Spacing();

    auto state = app.state();
    if (state == AppState::Disconnected) {
        if (ImGui::Button("Connect", ImVec2(-1, 28)))
            app.connect(server_url_buf_);
    }

    ImGui::Spacing();

    if (state == AppState::Connecting) {
        ImGui::TextColored({1.0f, 0.8f, 0.2f, 1.0f}, "Connecting...");
    } else if (state == AppState::Connected) {
        ImGui::TextColored({0.2f, 0.9f, 0.3f, 1.0f}, "Voice Connected");
        std::string sid = app.local_id();
        if (sid.size() > 8) sid = sid.substr(0, 8) + "...";
        ImGui::TextDisabled("%s", sid.c_str());
    } else {
        ImGui::TextDisabled("Not connected");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Text("Members");

    auto peers = app.peers();
    if (peers.empty() && state != AppState::Connected) {
        ImGui::TextDisabled("  --");
    } else {
        if (state == AppState::Connected) {
            std::string you = app.local_id();
            if (you.size() > 10) you = you.substr(0, 10) + "...";
            ImGui::TextColored({0.2f, 0.9f, 0.3f, 1.0f}, "  %s (you)", you.c_str());
        }
        for (auto& p : peers) {
            std::string name = p.id;
            if (name.size() > 10) name = name.substr(0, 10) + "...";
            ImVec4 col = p.connected ? ImVec4(0.2f, 0.9f, 0.3f, 1.0f)
                                     : ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
            ImGui::TextColored(col, "  %s", name.c_str());
        }
    }

    float bottom_h = 52.0f;
    float avail = ImGui::GetContentRegionAvail().y;
    if (avail > bottom_h) ImGui::Dummy(ImVec2(0, avail - bottom_h));

    render_sidebar_bottom(app);
}

void UIRenderer::render_sidebar_bottom(App& app) {
    ImGui::Separator();
    bool connected = (app.state() == AppState::Connected);
    ImGui::Spacing();

    if (!connected) {
        ImGui::TextDisabled("Not in voice");
        return;
    }

    float w = ImGui::GetContentRegionAvail().x;
    float btn = (w - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
    bool muted = app.muted();
    bool deaf  = app.deafened();

    auto push_toggle = [](bool on) {
        if (on) {
            ImGui::PushStyleColor(ImGuiCol_Button,        {0.85f, 0.25f, 0.25f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  {0.95f, 0.35f, 0.35f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,   {0.75f, 0.20f, 0.20f, 1.0f});
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button,        {0.28f, 0.30f, 0.33f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  {0.35f, 0.37f, 0.40f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,   {0.22f, 0.24f, 0.27f, 1.0f});
        }
    };

    push_toggle(muted);
    if (ImGui::Button(muted ? "MIC X" : "MIC", {btn, 30})) {
        if (deaf) app.toggle_deafen(); else app.toggle_mute();
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    push_toggle(deaf);
    if (ImGui::Button(deaf ? "DEAF" : "SND", {btn, 30}))
        app.toggle_deafen();
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.85f, 0.25f, 0.25f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  {0.95f, 0.35f, 0.35f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   {0.75f, 0.20f, 0.20f, 1.0f});
    if (ImGui::Button("END", {btn, 30}))
        app.disconnect();
    ImGui::PopStyleColor(3);
}

// ---------------------------------------------------------------------------
// Content panel (right side)
// ---------------------------------------------------------------------------

void UIRenderer::render_content(App& app) {
    bool connected = (app.state() == AppState::Connected);

    if (connected) {
        volume_ = app.volume();
        ImGui::SetNextItemWidth(160);
        if (ImGui::SliderFloat("Volume", &volume_, 0.0f, 2.0f, "%.1f"))
            app.set_volume(volume_);

        ImGui::SameLine(0, 20);
        bool m = app.muted();
        float in_lv  = std::min(app.input_level()  * 5.0f, 1.0f);
        float out_lv = std::min(app.output_level() * 5.0f, 1.0f);
        render_level_bar("Mic", in_lv,  m ? 0xFF4444AA : 0xFF44AA44);
        ImGui::SameLine(0, 12);
        render_level_bar("Spk", out_lv, app.deafened() ? 0xFF4444AA : 0xFF44AA44);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Screen sharing controls
        ImGui::Text("Screen Sharing");
        ImGui::Spacing();

        if (!app.sharing()) {
            ImVec4 green(0.20f, 0.70f, 0.30f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, green);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.25f, 0.80f, 0.35f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.15f, 0.60f, 0.25f, 1.0f});
            if (ImGui::Button("Share Screen", {160, 30})) {
                targets_ = ScreenCapture::list_targets();
                selected_target_ = -1;
                share_popup_open_ = true;
                // pre-load thumbnails
                for (size_t i = 0; i < targets_.size(); ++i) {
                    std::string tid = "__thumb_" + std::to_string(i) + "__";
                    auto frame = ScreenCapture::grab_thumbnail(targets_[i], 320, 180);
                    if (!frame.data.empty()) {
                        for (size_t j = 0; j < frame.data.size(); j += 4)
                            std::swap(frame.data[j], frame.data[j + 2]);
                        app.video_renderer().update_frame(tid, frame.data.data(),
                                                          frame.width, frame.height);
                    }
                }
            }
            ImGui::PopStyleColor(3);
        } else {
            ImGui::TextColored({0.2f, 0.9f, 0.3f, 1.0f}, "Sharing your screen");
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button,        {0.85f, 0.25f, 0.25f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  {0.95f, 0.35f, 0.35f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,   {0.75f, 0.20f, 0.20f, 1.0f});
            if (ImGui::Button("Stop Sharing", {160, 30}))
                app.stop_sharing();
            ImGui::PopStyleColor(3);
        }
    } else {
        ImGui::TextDisabled("Connect to a server to begin");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    render_video_panel(app);
}

// ---------------------------------------------------------------------------
// Share-screen popup (Discord-style picker)
// ---------------------------------------------------------------------------

void UIRenderer::render_share_popup(App& app) {
    if (!share_popup_open_) return;

    ImGui::OpenPopup("##SharePicker");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({700, 500}, ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("##SharePicker", &share_popup_open_,
                                ImGuiWindowFlags_NoTitleBar)) {
        ImGui::Text("Choose what to share");
        ImGui::Separator();
        ImGui::Spacing();

        // --- thumbnail grid ---
        float avail_w = ImGui::GetContentRegionAvail().x;
        int cols = std::max(1, static_cast<int>(avail_w / 180.0f));
        float thumb_w = (avail_w - ImGui::GetStyle().ItemSpacing.x * (cols - 1)) / cols;
        float thumb_h = thumb_w * (9.0f / 16.0f);

        ImGui::BeginChild("##Targets", {0, -80});
        for (int i = 0; i < static_cast<int>(targets_.size()); ++i) {
            if (i > 0 && (i % cols) != 0) ImGui::SameLine();

            ImGui::BeginGroup();
            ImGui::PushID(i);

            bool sel = (selected_target_ == i);
            if (sel) {
                ImGui::PushStyleColor(ImGuiCol_Button,        {0.20f, 0.40f, 0.80f, 1.0f});
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  {0.25f, 0.45f, 0.85f, 1.0f});
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,   {0.15f, 0.35f, 0.75f, 1.0f});
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button,        {0.22f, 0.23f, 0.26f, 1.0f});
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  {0.28f, 0.30f, 0.33f, 1.0f});
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,   {0.18f, 0.19f, 0.22f, 1.0f});
            }

            std::string tid = "__thumb_" + std::to_string(i) + "__";
            auto tex = app.video_renderer().texture(tid);
            if (tex) {
                ImGui::ImageButton("##tb", tex, {thumb_w - 8, thumb_h});
            } else {
                ImGui::Button("##tb", {thumb_w - 8, thumb_h});
            }
            if (ImGui::IsItemClicked()) selected_target_ = i;

            ImGui::PopStyleColor(3);

            std::string label = targets_[i].name;
            if (label.size() > 22) label = label.substr(0, 22) + "...";
            ImGui::TextWrapped("%s", label.c_str());
            ImGui::PopID();
            ImGui::EndGroup();
        }
        ImGui::EndChild();

        // --- bottom bar: settings + Go ---
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Quality:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        if (ImGui::BeginCombo("##res", kStreamPresets[selected_preset_].label)) {
            for (int i = 0; i < kStreamPresetCount; ++i) {
                bool s = (selected_preset_ == i);
                if (ImGui::Selectable(kStreamPresets[i].label, s))
                    selected_preset_ = i;
                if (s) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine(0, 20);
        ImGui::Text("FPS:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        char fps_label[16];
        std::snprintf(fps_label, sizeof(fps_label), "%d", kFpsOptions[selected_fps_]);
        if (ImGui::BeginCombo("##fps", fps_label)) {
            for (int i = 0; i < kFpsOptionCount; ++i) {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%d", kFpsOptions[i]);
                bool s = (selected_fps_ == i);
                if (ImGui::Selectable(buf, s))
                    selected_fps_ = i;
                if (s) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine(0, 30);

        bool can_go = selected_target_ >= 0 &&
                      selected_target_ < static_cast<int>(targets_.size());
        if (!can_go) ImGui::BeginDisabled();

        ImVec4 green(0.20f, 0.70f, 0.30f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, green);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.25f, 0.80f, 0.35f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.15f, 0.60f, 0.25f, 1.0f});
        if (ImGui::Button("Go Live", {100, 28})) {
            int fps = kFpsOptions[selected_fps_];
            app.start_sharing(targets_[selected_target_], selected_preset_, fps);
            // cleanup thumbnails
            for (size_t i = 0; i < targets_.size(); ++i) {
                app.video_renderer().remove_peer("__thumb_" + std::to_string(i) + "__");
            }
            share_popup_open_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(3);

        if (!can_go) ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Cancel", {80, 28})) {
            for (size_t i = 0; i < targets_.size(); ++i) {
                app.video_renderer().remove_peer("__thumb_" + std::to_string(i) + "__");
            }
            share_popup_open_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------------------
// Video panel with OSD overlay
// ---------------------------------------------------------------------------

void UIRenderer::render_video_panel(App& app) {
    auto active = app.video_renderer().active_peers();
    if (active.empty() && !app.sharing()) return;

    ImGui::Text("Video Streams");
    ImGui::Spacing();

    ImVec2 avail = ImGui::GetContentRegionAvail();

    int count = static_cast<int>(active.size());
    if (count == 0) {
        ImGui::TextDisabled("You are sharing your screen");
        return;
    }

    int cols = (count <= 1) ? 1 : 2;
    float tile_w = (avail.x - ImGui::GetStyle().ItemSpacing.x * static_cast<float>(cols - 1))
                   / static_cast<float>(cols);
    float tile_h = tile_w * (9.0f / 16.0f);

    ImDrawList* draw = ImGui::GetWindowDrawList();

    for (int i = 0; i < count; ++i) {
        const auto& pid = active[static_cast<size_t>(i)];
        auto tex = app.video_renderer().texture(pid);
        if (!tex) continue;

        if (i > 0 && (i % cols) != 0) ImGui::SameLine();

        ImGui::BeginGroup();

        ImVec2 img_pos = ImGui::GetCursorScreenPos();
        ImGui::Image(tex, {tile_w, tile_h});

        // OSD overlay: resolution, bitrate, codec
        auto stats = app.stream_stats(pid);
        if (stats.width > 0) {
            char osd[128];
            std::snprintf(osd, sizeof(osd), "%dx%d  H.264  %d kbps",
                          stats.width, stats.height, stats.measured_kbps);

            ImVec2 text_sz = ImGui::CalcTextSize(osd);
            ImVec2 osd_pos = {img_pos.x + 6, img_pos.y + 4};

            draw->AddRectFilled(
                {osd_pos.x - 2, osd_pos.y - 1},
                {osd_pos.x + text_sz.x + 4, osd_pos.y + text_sz.y + 2},
                IM_COL32(0, 0, 0, 180), 3.0f);
            draw->AddText(osd_pos, IM_COL32(220, 220, 220, 255), osd);
        }

        std::string label = pid;
        if (label.size() > 12) label = label.substr(0, 12) + "...";
        ImGui::TextDisabled("%s", label.c_str());
        ImGui::EndGroup();
    }
}

void UIRenderer::render_level_bar(const char* label, float level, unsigned int color) {
    ImGui::Text("%s", label);
    ImGui::SameLine();

    ImVec2 pos = ImGui::GetCursorScreenPos();
    float bw = 80.0f, bh = 12.0f;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(pos, {pos.x + bw, pos.y + bh}, IM_COL32(40, 42, 46, 255), 3.0f);
    if (level > 0.001f)
        draw->AddRectFilled(pos, {pos.x + bw * level, pos.y + bh}, color, 3.0f);

    ImGui::Dummy({bw, bh});
}
