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

    auto* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.18f, 0.19f, 0.21f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.18f, 0.19f, 0.21f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.17f, 0.18f, 0.20f, 1.00f);
    colors[ImGuiCol_Border] = ImVec4(0.12f, 0.13f, 0.14f, 1.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.25f, 0.27f, 0.30f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.30f, 0.32f, 0.35f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.35f, 0.37f, 0.40f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.15f, 0.16f, 0.17f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.18f, 0.19f, 0.21f, 1.00f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.18f, 0.19f, 0.21f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.15f, 0.16f, 0.17f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.28f, 0.30f, 0.33f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.33f, 0.35f, 0.38f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.34f, 0.54f, 0.93f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.34f, 0.54f, 0.93f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.44f, 0.64f, 1.00f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.34f, 0.54f, 0.93f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.40f, 0.60f, 1.00f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.28f, 0.48f, 0.88f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.25f, 0.27f, 0.30f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.32f, 0.35f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.35f, 0.37f, 0.40f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.12f, 0.13f, 0.14f, 1.00f);
    colors[ImGuiCol_Text] = ImVec4(0.90f, 0.91f, 0.92f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.51f, 0.52f, 1.00f);
}

void UIRenderer::render(App& app) {
    app.update();

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("##Main", nullptr, flags);

    float sidebar_width = 240.0f;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.16f, 0.17f, 0.19f, 1.00f));
    ImGui::BeginChild("##Sidebar", ImVec2(sidebar_width, 0));
    render_sidebar(app);
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::SameLine();

    ImGui::BeginChild("##Content", ImVec2(0, 0));
    render_content(app);
    ImGui::EndChild();

    ImGui::End();
}

void UIRenderer::render_sidebar(App& app) {
    ImGui::TextColored(ImVec4(0.34f, 0.54f, 0.93f, 1.0f), "DRISCORD");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Server");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##server", server_url_buf_, sizeof(server_url_buf_));
    ImGui::Spacing();

    auto state = app.state();

    if (state == AppState::Disconnected) {
        if (ImGui::Button("Connect", ImVec2(-1, 28))) {
            app.connect(server_url_buf_);
        }
    }

    ImGui::Spacing();

    if (state == AppState::Connecting) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Connecting...");
    } else if (state == AppState::Connected) {
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "Voice Connected");
        std::string short_id = app.local_id();
        if (short_id.size() > 8) short_id = short_id.substr(0, 8) + "...";
        ImGui::TextDisabled("%s", short_id.c_str());
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
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "  %s (you)", you.c_str());
        }
        for (auto& p : peers) {
            std::string name = p.id;
            if (name.size() > 10) name = name.substr(0, 10) + "...";
            ImVec4 col = p.connected ? ImVec4(0.2f, 0.9f, 0.3f, 1.0f)
                                     : ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
            ImGui::TextColored(col, "  %s", name.c_str());
        }
    }

    // --- bottom bar (fixed at bottom of sidebar) ---
    float bottom_bar_height = 52.0f;
    float avail_y = ImGui::GetContentRegionAvail().y;
    if (avail_y > bottom_bar_height) {
        ImGui::Dummy(ImVec2(0, avail_y - bottom_bar_height));
    }

    render_sidebar_bottom(app);
}

void UIRenderer::render_sidebar_bottom(App& app) {
    ImGui::Separator();

    auto state = app.state();
    bool connected = (state == AppState::Connected);

    ImGui::Spacing();

    if (connected) {
        float avail_w = ImGui::GetContentRegionAvail().x;
        float btn_w = (avail_w - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;

        bool muted = app.muted();
        bool deafened = app.deafened();

        auto push_toggle_color = [](bool active) {
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.20f, 0.20f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.28f, 0.30f, 0.33f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.37f, 0.40f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.24f, 0.27f, 1.0f));
            }
        };

        push_toggle_color(muted);
        if (ImGui::Button(muted ? "MIC X" : "MIC", ImVec2(btn_w, 30))) {
            if (deafened) {
                app.toggle_deafen();
            } else {
                app.toggle_mute();
            }
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine();

        push_toggle_color(deafened);
        if (ImGui::Button(deafened ? "DEAF" : "SND", ImVec2(btn_w, 30))) {
            app.toggle_deafen();
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.20f, 0.20f, 1.0f));
        if (ImGui::Button("END", ImVec2(btn_w, 30))) {
            app.disconnect();
        }
        ImGui::PopStyleColor(3);
    } else {
        ImGui::TextDisabled("Not in voice");
    }
}

void UIRenderer::render_content(App& app) {
    bool connected = (app.state() == AppState::Connected);

    if (connected) {
        // Volume control
        volume_ = app.volume();
        ImGui::SetNextItemWidth(160);
        if (ImGui::SliderFloat("Volume", &volume_, 0.0f, 2.0f, "%.1f")) {
            app.set_volume(volume_);
        }

        ImGui::SameLine(0, 20);

        bool muted = app.muted();
        float in_level = std::min(app.input_level() * 5.0f, 1.0f);
        float out_level = std::min(app.output_level() * 5.0f, 1.0f);
        render_level_bar("Mic", in_level, muted ? 0xFF4444AA : 0xFF44AA44);
        ImGui::SameLine(0, 12);
        render_level_bar("Spk", out_level, app.deafened() ? 0xFF4444AA : 0xFF44AA44);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        render_screen_sharing(app);
    } else {
        ImGui::TextDisabled("Connect to a server to begin");
    }

    ImGui::Separator();
    ImGui::Spacing();
    render_video_panel(app);
}

void UIRenderer::render_screen_sharing(App& app) {
    ImGui::Text("Screen Sharing");
    ImGui::Spacing();

    bool sharing = app.sharing();

    if (!sharing) {
        ImGui::SetNextItemWidth(300);
        auto getter = [](void* data, int idx) -> const char* {
            auto* v = static_cast<std::vector<CaptureTarget>*>(data);
            return (*v)[static_cast<size_t>(idx)].name.c_str();
        };

        bool combo_changed = ImGui::Combo("##target", &selected_target_, getter,
                                           &targets_, static_cast<int>(targets_.size()));

        ImGui::SameLine();
        bool refreshed = ImGui::Button("Refresh");
        if (refreshed) {
            targets_ = ScreenCapture::list_targets();
            selected_target_ = 0;
            last_preview_idx_ = -1;
            app.clear_preview();
        }

        if (targets_.empty()) {
            ImGui::TextDisabled("Press Refresh to load capture targets");
        }

        bool has_target = !targets_.empty() &&
                          selected_target_ >= 0 &&
                          selected_target_ < static_cast<int>(targets_.size());

        if (has_target && (combo_changed || refreshed || last_preview_idx_ != selected_target_)) {
            last_preview_idx_ = selected_target_;
            app.update_preview(targets_[static_cast<size_t>(selected_target_)]);
        }

        if (has_target) {
            auto preview_tex = app.video_renderer().texture(kPreviewPeerId);
            if (preview_tex) {
                ImGui::Spacing();
                auto sz = app.video_renderer().frame_size(kPreviewPeerId);
                float aspect = (sz.y > 0) ? sz.x / sz.y : 16.0f / 9.0f;
                float preview_w = std::min(300.0f, ImGui::GetContentRegionAvail().x);
                float preview_h = preview_w / aspect;
                ImGui::Image(preview_tex, ImVec2(preview_w, preview_h));
            }
        }

        ImGui::Spacing();

        if (has_target) {
            ImVec4 green(0.20f, 0.70f, 0.30f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, green);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.80f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.60f, 0.25f, 1.0f));
            if (ImGui::Button("Share Screen", ImVec2(160, 30))) {
                app.clear_preview();
                last_preview_idx_ = -1;
                app.start_sharing(targets_[static_cast<size_t>(selected_target_)]);
            }
            ImGui::PopStyleColor(3);
        }
    } else {
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "Sharing your screen");
        ImGui::Spacing();
        ImVec4 red(0.85f, 0.25f, 0.25f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, red);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.20f, 0.20f, 1.0f));
        if (ImGui::Button("Stop Sharing", ImVec2(160, 30))) {
            app.stop_sharing();
        }
        ImGui::PopStyleColor(3);
    }

    ImGui::Spacing();
}

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
    float tile_w = (avail.x - ImGui::GetStyle().ItemSpacing.x * static_cast<float>(cols - 1)) /
                   static_cast<float>(cols);
    float tile_h = tile_w * (9.0f / 16.0f);

    for (int i = 0; i < count; ++i) {
        const auto& peer_id = active[static_cast<size_t>(i)];
        auto tex = app.video_renderer().texture(peer_id);
        if (!tex) continue;

        if (i > 0 && (i % cols) != 0) {
            ImGui::SameLine();
        }

        ImGui::BeginGroup();
        ImGui::Image(tex, ImVec2(tile_w, tile_h));
        std::string label = peer_id;
        if (label.size() > 12) label = label.substr(0, 12) + "...";
        ImGui::TextDisabled("%s", label.c_str());
        ImGui::EndGroup();
    }
}

void UIRenderer::render_level_bar(const char* label, float level, unsigned int color) {
    ImGui::Text("%s", label);
    ImGui::SameLine();

    ImVec2 pos = ImGui::GetCursorScreenPos();
    float bar_width = 80.0f;
    float bar_height = 12.0f;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(pos, ImVec2(pos.x + bar_width, pos.y + bar_height),
                        IM_COL32(40, 42, 46, 255), 3.0f);
    if (level > 0.001f) {
        draw->AddRectFilled(pos, ImVec2(pos.x + bar_width * level, pos.y + bar_height),
                            color, 3.0f);
    }

    ImGui::Dummy(ImVec2(bar_width, bar_height));
}
