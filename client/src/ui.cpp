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
    c[ImGuiCol_WindowBg] = {0.18f, 0.19f, 0.21f, 1.00f};
    c[ImGuiCol_ChildBg] = {0.18f, 0.19f, 0.21f, 1.00f};
    c[ImGuiCol_PopupBg] = {0.15f, 0.16f, 0.18f, 1.00f};
    c[ImGuiCol_Border] = {0.12f, 0.13f, 0.14f, 1.00f};
    c[ImGuiCol_FrameBg] = {0.25f, 0.27f, 0.30f, 1.00f};
    c[ImGuiCol_FrameBgHovered] = {0.30f, 0.32f, 0.35f, 1.00f};
    c[ImGuiCol_FrameBgActive] = {0.35f, 0.37f, 0.40f, 1.00f};
    c[ImGuiCol_TitleBg] = {0.15f, 0.16f, 0.17f, 1.00f};
    c[ImGuiCol_TitleBgActive] = {0.18f, 0.19f, 0.21f, 1.00f};
    c[ImGuiCol_ScrollbarBg] = {0.15f, 0.16f, 0.17f, 1.00f};
    c[ImGuiCol_ScrollbarGrab] = {0.28f, 0.30f, 0.33f, 1.00f};
    c[ImGuiCol_ScrollbarGrabHovered] = {0.33f, 0.35f, 0.38f, 1.00f};
    c[ImGuiCol_CheckMark] = {0.34f, 0.54f, 0.93f, 1.00f};
    c[ImGuiCol_SliderGrab] = {0.34f, 0.54f, 0.93f, 1.00f};
    c[ImGuiCol_SliderGrabActive] = {0.44f, 0.64f, 1.00f, 1.00f};
    c[ImGuiCol_Button] = {0.34f, 0.54f, 0.93f, 1.00f};
    c[ImGuiCol_ButtonHovered] = {0.40f, 0.60f, 1.00f, 1.00f};
    c[ImGuiCol_ButtonActive] = {0.28f, 0.48f, 0.88f, 1.00f};
    c[ImGuiCol_Header] = {0.25f, 0.27f, 0.30f, 1.00f};
    c[ImGuiCol_HeaderHovered] = {0.30f, 0.32f, 0.35f, 1.00f};
    c[ImGuiCol_HeaderActive] = {0.35f, 0.37f, 0.40f, 1.00f};
    c[ImGuiCol_Separator] = {0.12f, 0.13f, 0.14f, 1.00f};
    c[ImGuiCol_Text] = {0.90f, 0.91f, 0.92f, 1.00f};
    c[ImGuiCol_TextDisabled] = {0.50f, 0.51f, 0.52f, 1.00f};
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
        if (ImGui::Button("Connect", ImVec2(-1, 28))) {
            app.connect(server_url_buf_);
        }
    }

    ImGui::Spacing();

    if (state == AppState::Connecting) {
        ImGui::TextColored({1.0f, 0.8f, 0.2f, 1.0f}, "Connecting...");
    } else if (state == AppState::Connected) {
        ImGui::TextColored({0.2f, 0.9f, 0.3f, 1.0f}, "Voice Connected");
        std::string sid = app.local_id();
        if (sid.size() > 8) {
            sid = sid.substr(0, 8) + "...";
        }
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
            std::string you_short = you;
            if (you_short.size() > 10) {
                you_short = you_short.substr(0, 10) + "...";
            }
            char label[128];
            std::snprintf(label, sizeof(label), "  %s (you)##user_%s", you_short.c_str(), you.c_str());
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.9f, 0.3f, 1.0f));
            ImGui::Selectable(label, false, ImGuiSelectableFlags_None, ImVec2(0, 0));
            ImGui::PopStyleColor();
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                popup_peer_id_ = you;
                popup_is_self_ = true;
                volume_ = app.volume();
                ImGui::OpenPopup("##UserPopup");
            }
        }
        for (auto& p : peers) {
            std::string name = p.id;
            if (name.size() > 10) {
                name = name.substr(0, 10) + "...";
            }
            char label[128];
            std::snprintf(label, sizeof(label), "  %s##user_%s", name.c_str(), p.id.c_str());
            ImVec4 col = p.connected ? ImVec4(0.2f, 0.9f, 0.3f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::Selectable(label, false, ImGuiSelectableFlags_None, ImVec2(0, 0));
            ImGui::PopStyleColor();
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                popup_peer_id_ = p.id;
                popup_is_self_ = false;
                popup_peer_vol_ = app.peer_volume(p.id);
                ImGui::OpenPopup("##UserPopup");
            }
        }

        render_user_popup(app);
    }

    float bottom_h = 52.0f;
    float avail = ImGui::GetContentRegionAvail().y;
    if (avail > bottom_h) {
        ImGui::Dummy(ImVec2(0, avail - bottom_h));
    }

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
    bool deaf = app.deafened();

    auto push_toggle = [](bool on) {
        if (on) {
            ImGui::PushStyleColor(ImGuiCol_Button, {0.85f, 0.25f, 0.25f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.95f, 0.35f, 0.35f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, {0.75f, 0.20f, 0.20f, 1.0f});
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, {0.28f, 0.30f, 0.33f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.35f, 0.37f, 0.40f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, {0.22f, 0.24f, 0.27f, 1.0f});
        }
    };

    push_toggle(muted);
    if (ImGui::Button(muted ? "MIC X" : "MIC", {btn, 30})) {
        if (deaf) {
            app.toggle_deafen();
        } else {
            app.toggle_mute();
        }
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    push_toggle(deaf);
    if (ImGui::Button(deaf ? "DEAF" : "SND", {btn, 30})) {
        app.toggle_deafen();
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, {0.85f, 0.25f, 0.25f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.95f, 0.35f, 0.35f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, {0.75f, 0.20f, 0.20f, 1.0f});
    if (ImGui::Button("END", {btn, 30})) {
        app.disconnect();
    }
    ImGui::PopStyleColor(3);
}

// ---------------------------------------------------------------------------
// Content panel (right side)
// ---------------------------------------------------------------------------

void UIRenderer::render_content(App& app) {
    bool connected = (app.state() == AppState::Connected);

    if (!connected) {
        ImGui::TextDisabled("Connect to a server to begin");
        return;
    }

    if (!app.sharing()) {
        ImVec4 green(0.20f, 0.70f, 0.30f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, green);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.25f, 0.80f, 0.35f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, {0.15f, 0.60f, 0.25f, 1.0f});
        if (ImGui::Button("Share Screen", {140, 26})) {
            targets_ = ScreenCapture::list_targets();
            selected_target_ = -1;
            share_popup_open_ = true;
            for (size_t i = 0; i < targets_.size(); ++i) {
                std::string tid = "__thumb_" + std::to_string(i) + "__";
                auto frame = ScreenCapture::grab_thumbnail(targets_[i], 320, 180);
                if (!frame.data.empty()) {
                    for (size_t j = 0; j < frame.data.size(); j += 4) {
                        std::swap(frame.data[j], frame.data[j + 2]);
                    }
                    app.video_renderer().update_frame(tid, frame.data.data(), frame.width, frame.height);
                }
            }
        }
        ImGui::PopStyleColor(3);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, {0.85f, 0.25f, 0.25f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.95f, 0.35f, 0.35f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, {0.75f, 0.20f, 0.20f, 1.0f});
        if (ImGui::Button("Stop Sharing", {140, 26})) {
            app.stop_sharing();
        }
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
        ImGui::TextColored({0.2f, 0.9f, 0.3f, 1.0f}, "LIVE");
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
    if (!share_popup_open_) {
        return;
    }

    ImGui::OpenPopup("##SharePicker");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({700, 500}, ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("##SharePicker", &share_popup_open_, ImGuiWindowFlags_NoTitleBar)) {
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
            if (i > 0 && (i % cols) != 0) {
                ImGui::SameLine();
            }

            ImGui::BeginGroup();
            ImGui::PushID(i);

            bool sel = (selected_target_ == i);
            if (sel) {
                ImGui::PushStyleColor(ImGuiCol_Button, {0.20f, 0.40f, 0.80f, 1.0f});
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.25f, 0.45f, 0.85f, 1.0f});
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, {0.15f, 0.35f, 0.75f, 1.0f});
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, {0.22f, 0.23f, 0.26f, 1.0f});
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.28f, 0.30f, 0.33f, 1.0f});
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, {0.18f, 0.19f, 0.22f, 1.0f});
            }

            std::string tid = "__thumb_" + std::to_string(i) + "__";
            auto tex = app.video_renderer().texture(tid);
            if (tex) {
                ImGui::ImageButton("##tb", tex, {thumb_w - 8, thumb_h});
            } else {
                ImGui::Button("##tb", {thumb_w - 8, thumb_h});
            }
            if (ImGui::IsItemClicked()) {
                selected_target_ = i;
                if (targets_[i].type == CaptureTarget::Window) {
                    selected_quality_ = StreamQuality::Source;
                }
            }

            ImGui::PopStyleColor(3);

            std::string label = targets_[i].name;
            if (label.size() > 22) {
                label = label.substr(0, 22) + "...";
            }
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
        int qi = static_cast<int>(selected_quality_);
        if (ImGui::BeginCombo("##res", kStreamPresets[qi].label)) {
            for (int i = 0; i < kStreamPresetCount; ++i) {
                bool s = (qi == i);
                if (ImGui::Selectable(kStreamPresets[i].label, s)) {
                    selected_quality_ = static_cast<StreamQuality>(i);
                }
                if (s) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine(0, 20);
        ImGui::Text("FPS:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        int fi = static_cast<int>(selected_fps_);
        char fps_label[16];
        std::snprintf(fps_label, sizeof(fps_label), "%d", kFpsValues[fi]);
        if (ImGui::BeginCombo("##fps", fps_label)) {
            for (int i = 0; i < kFpsOptionCount; ++i) {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%d", kFpsValues[i]);
                bool s = (fi == i);
                if (ImGui::Selectable(buf, s)) {
                    selected_fps_ = static_cast<FrameRate>(i);
                }
                if (s) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine(0, 20);

        if (App::system_audio_available()) {
            ImGui::Checkbox("Share Audio", &share_audio_);
            ImGui::SameLine(0, 20);
        }

        bool can_go = selected_target_ >= 0 && selected_target_ < static_cast<int>(targets_.size());
        if (!can_go) {
            ImGui::BeginDisabled();
        }

        ImVec4 green(0.20f, 0.70f, 0.30f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, green);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.25f, 0.80f, 0.35f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, {0.15f, 0.60f, 0.25f, 1.0f});
        if (ImGui::Button("Go Live", {100, 28})) {
            int fps = fps_value(selected_fps_);
            app.start_sharing(targets_[selected_target_], selected_quality_, fps, share_audio_);
            // cleanup thumbnails
            for (size_t i = 0; i < targets_.size(); ++i) {
                app.video_renderer().remove_peer("__thumb_" + std::to_string(i) + "__");
            }
            share_popup_open_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(3);

        if (!can_go) {
            ImGui::EndDisabled();
        }

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
// Video panel: Discord-style grid of user tiles + stream tiles
// ---------------------------------------------------------------------------

namespace {

ImU32 peer_color(const std::string& id) {
    uint32_t h = 0x811c9dc5;
    for (char c : id) {
        h ^= static_cast<uint32_t>(c);
        h *= 0x01000193;
    }
    auto r = static_cast<uint8_t>(80 + (h & 0x7F));
    auto g = static_cast<uint8_t>(80 + ((h >> 8) & 0x7F));
    auto b = static_cast<uint8_t>(80 + ((h >> 16) & 0x7F));
    return IM_COL32(r, g, b, 255);
}

std::string short_id(const std::string& id, size_t max_len = 10) {
    if (id.size() <= max_len) {
        return id;
    }
    return id.substr(0, max_len) + "...";
}

}  // namespace

void UIRenderer::render_video_panel(App& app) {
    struct Tile {
        std::string id;
        std::string peer_id;
        std::string label;
        bool is_stream;
        ImU32 color;
    };

    std::vector<Tile> tiles;
    auto streaming_peers = app.video_renderer().active_peers();

    auto is_streaming = [&](const std::string& pid) {
        return std::find(streaming_peers.begin(), streaming_peers.end(), pid) != streaming_peers.end();
    };

    // User tile for self
    std::string local = app.local_id();
    if (!local.empty()) {
        Tile t;
        t.id = local;
        t.peer_id = local;
        t.label = short_id(local) + " (you)";
        t.is_stream = false;
        t.color = peer_color(local);
        tiles.push_back(std::move(t));
    }

    // User tiles for peers
    auto peers = app.peers();
    for (auto& p : peers) {
        Tile t;
        t.id = p.id;
        t.peer_id = p.id;
        t.label = short_id(p.id);
        t.is_stream = false;
        t.color = peer_color(p.id);
        tiles.push_back(std::move(t));
    }

    // Stream tiles for peers with active video
    for (auto& spid : streaming_peers) {
        Tile t;
        t.id = "stream_" + spid;
        t.peer_id = spid;
        t.label = short_id(spid);
        t.is_stream = true;
        t.color = 0;
        tiles.push_back(std::move(t));
    }

    if (tiles.empty()) {
        return;
    }

    // Validate focused tile still exists
    if (!focused_tile_id_.empty()) {
        bool found = false;
        for (auto& t : tiles) {
            if (t.id == focused_tile_id_) {
                found = true;
                break;
            }
        }
        if (!found) {
            focused_tile_id_.clear();
        }
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 avail = ImGui::GetContentRegionAvail();

    // --- Fullscreen mode ---
    if (!focused_tile_id_.empty()) {
        for (auto& t : tiles) {
            if (t.id != focused_tile_id_) {
                continue;
            }

            ImGui::PushID(t.id.c_str());

            if (t.is_stream) {
                auto tex = app.video_renderer().texture(t.peer_id);
                if (tex) {
                    ImVec2 frame_sz = app.video_renderer().frame_size(t.peer_id);
                    float aspect = (frame_sz.x > 0 && frame_sz.y > 0) ? frame_sz.y / frame_sz.x : 9.0f / 16.0f;

                    float fw = avail.x;
                    float fh = fw * aspect;
                    if (fh > avail.y - 20.0f) {
                        fh = avail.y - 20.0f;
                        fw = fh / aspect;
                    }

                    float ox = (avail.x - fw) * 0.5f;
                    if (ox > 0) {
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ox);
                    }

                    ImVec2 img_pos = ImGui::GetCursorScreenPos();
                    ImGui::Image(tex, {fw, fh});

                    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                        focused_tile_id_.clear();
                    }
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                        stream_popup_peer_ = t.peer_id;
                        stream_popup_vol_ = app.stream_volume();
                        ImGui::OpenPopup("##StreamVolPopup");
                    }

                    render_stream_osd(app, t.peer_id, draw, img_pos, fw);

                    char fs_label[128];
                    std::snprintf(fs_label, sizeof(fs_label), "%s  LIVE", t.label.c_str());
                    ImGui::TextDisabled("%s", fs_label);
                }
            } else {
                float sz = std::min(avail.x, avail.y - 20.0f);
                float ox = (avail.x - sz) * 0.5f;
                if (ox > 0) {
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ox);
                }

                ImVec2 pos = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton("##fs_tile", {sz, sz});
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                    focused_tile_id_.clear();
                }

                draw->AddRectFilled(pos, {pos.x + sz, pos.y + sz}, t.color, 8.0f);

                ImVec2 text_sz = ImGui::CalcTextSize(t.label.c_str());
                ImVec2 text_pos = {pos.x + (sz - text_sz.x) * 0.5f, pos.y + (sz - text_sz.y) * 0.5f};
                draw->AddText(text_pos, IM_COL32(255, 255, 255, 230), t.label.c_str());

                if (is_streaming(t.peer_id)) {
                    const char* live = "LIVE";
                    ImVec2 lsz = ImGui::CalcTextSize(live);
                    ImVec2 lpos = {pos.x + sz - lsz.x - 10, pos.y + 8};
                    draw->AddRectFilled(
                        {lpos.x - 4, lpos.y - 2},
                        {lpos.x + lsz.x + 4, lpos.y + lsz.y + 2},
                        IM_COL32(220, 50, 50, 220),
                        3.0f
                    );
                    draw->AddText(lpos, IM_COL32(255, 255, 255, 255), live);
                }

                ImGui::TextDisabled("%s", t.label.c_str());
            }

            render_stream_volume_popup(app);
            ImGui::PopID();
            break;
        }
        return;
    }

    // --- Grid mode ---
    int count = static_cast<int>(tiles.size());
    int cols = 1;
    if (count >= 10) {
        cols = 4;
    } else if (count >= 5) {
        cols = 3;
    } else if (count >= 2) {
        cols = 2;
    }

    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float tile_w = (avail.x - spacing * static_cast<float>(cols - 1)) / static_cast<float>(cols);
    float tile_h = tile_w * (9.0f / 16.0f);

    for (int i = 0; i < count; ++i) {
        auto& t = tiles[static_cast<size_t>(i)];

        if (i > 0 && (i % cols) != 0) {
            ImGui::SameLine();
        }

        ImGui::PushID(t.id.c_str());
        ImGui::BeginGroup();

        if (t.is_stream) {
            auto tex = app.video_renderer().texture(t.peer_id);
            if (tex) {
                ImVec2 frame_sz = app.video_renderer().frame_size(t.peer_id);
                float aspect = (frame_sz.x > 0 && frame_sz.y > 0) ? frame_sz.y / frame_sz.x : 9.0f / 16.0f;
                float th = tile_w * aspect;

                ImVec2 img_pos = ImGui::GetCursorScreenPos();
                ImGui::Image(tex, {tile_w, th});

                if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                    focused_tile_id_ = t.id;
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    stream_popup_peer_ = t.peer_id;
                    stream_popup_vol_ = app.peer_volume(t.peer_id);
                    ImGui::OpenPopup("##StreamVolPopup");
                }

                render_stream_osd(app, t.peer_id, draw, img_pos, tile_w);

                const char* live = "LIVE";
                ImVec2 lsz = ImGui::CalcTextSize(live);
                ImVec2 lpos = {img_pos.x + tile_w - lsz.x - 8, img_pos.y + 5};
                draw->AddRectFilled(
                    {lpos.x - 3, lpos.y - 1},
                    {lpos.x + lsz.x + 3, lpos.y + lsz.y + 1},
                    IM_COL32(220, 50, 50, 220),
                    3.0f
                );
                draw->AddText(lpos, IM_COL32(255, 255, 255, 255), live);

                char slabel[128];
                std::snprintf(slabel, sizeof(slabel), "%s", t.label.c_str());
                ImGui::TextDisabled("%s", slabel);
            }
        } else {
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##tile", {tile_w, tile_h});

            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                focused_tile_id_ = t.id;
            }

            draw->AddRectFilled(pos, {pos.x + tile_w, pos.y + tile_h}, t.color, 6.0f);

            ImVec2 hover_min = pos;
            ImVec2 hover_max = {pos.x + tile_w, pos.y + tile_h};
            if (ImGui::IsItemHovered()) {
                draw->AddRect(hover_min, hover_max, IM_COL32(255, 255, 255, 60), 6.0f, 0, 2.0f);
            }

            ImVec2 text_sz = ImGui::CalcTextSize(t.label.c_str());
            ImVec2 text_pos = {pos.x + (tile_w - text_sz.x) * 0.5f, pos.y + (tile_h - text_sz.y) * 0.5f};
            draw->AddText(text_pos, IM_COL32(255, 255, 255, 220), t.label.c_str());

            if (is_streaming(t.peer_id)) {
                const char* live = "LIVE";
                ImVec2 lsz = ImGui::CalcTextSize(live);
                ImVec2 lpos = {pos.x + tile_w - lsz.x - 8, pos.y + 5};
                draw->AddRectFilled(
                    {lpos.x - 3, lpos.y - 1},
                    {lpos.x + lsz.x + 3, lpos.y + lsz.y + 1},
                    IM_COL32(220, 50, 50, 220),
                    3.0f
                );
                draw->AddText(lpos, IM_COL32(255, 255, 255, 255), live);
            }

            ImGui::TextDisabled("%s", t.label.c_str());
        }

        render_stream_volume_popup(app);

        ImGui::EndGroup();
        ImGui::PopID();
    }
}

void UIRenderer::render_stream_volume_popup(App& app) {
    if (ImGui::BeginPopup("##StreamVolPopup")) {
        std::string display = short_id(stream_popup_peer_, 14);
        ImGui::TextColored({0.34f, 0.54f, 0.93f, 1.0f}, "%s", display.c_str());
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Stream Volume");
        ImGui::SetNextItemWidth(150);
        if (ImGui::SliderFloat("##stream_vol", &stream_popup_vol_, 0.0f, 2.0f, "%.1f")) {
            app.set_stream_volume(stream_popup_vol_);
        }

        ImGui::EndPopup();
    }
}

void UIRenderer::render_user_popup(App& app) {
    if (ImGui::BeginPopup("##UserPopup")) {
        std::string display = popup_peer_id_;
        if (display.size() > 14) {
            display = display.substr(0, 14) + "...";
        }
        ImGui::TextColored({0.34f, 0.54f, 0.93f, 1.0f}, "%s", display.c_str());
        ImGui::Separator();
        ImGui::Spacing();

        if (popup_is_self_) {
            ImGui::Text("Mic Volume");
            ImGui::SetNextItemWidth(150);
            if (ImGui::SliderFloat("##mic_vol", &volume_, 0.0f, 2.0f, "%.1f")) {
                app.set_volume(volume_);
            }

            ImGui::Spacing();
            float in_lv = std::min(app.input_level() * 5.0f, 1.0f);
            float out_lv = std::min(app.output_level() * 5.0f, 1.0f);
            render_level_bar("Mic", in_lv, app.muted() ? 0xFF4444AA : 0xFF44AA44);
            render_level_bar("Spk", out_lv, app.deafened() ? 0xFF4444AA : 0xFF44AA44);
        } else {
            ImGui::Text("User Volume");
            ImGui::SetNextItemWidth(150);
            if (ImGui::SliderFloat("##peer_vol", &popup_peer_vol_, 0.0f, 2.0f, "%.1f")) {
                app.set_peer_volume(popup_peer_id_, popup_peer_vol_);
            }
        }

        ImGui::EndPopup();
    }
}

void UIRenderer::render_stream_osd(
    App& app,
    const std::string& peer_id,
    ImDrawList* draw,
    ImVec2 img_pos,
    float /*img_w*/
) {
    constexpr int64_t kRefreshMs = 500;
    if (utils::ElapsedMs(stats_last_update_) >= kRefreshMs) {
        stats_cache_ = app.stream_stats(peer_id);
        stats_last_update_ = utils::Now();
    }

    const auto& s = stats_cache_;
    if (s.width == 0) {
        return;
    }

    char line1[128];
    std::snprintf(line1, sizeof(line1), "%dx%d  H.264  %d kbps", s.width, s.height, s.measured_kbps);

    const auto& j = s.jitter;
    char line2[512];
    std::snprintf(
        line2,
        sizeof(line2),
        "V: q=%d buf=%dms%s  A: q=%d buf=%dms%s",
        j.video_queue,
        j.video_buf_ms,
        j.video_queue == 0 ? " [wait]" : "",
        j.audio_queue,
        j.audio_buf_ms,
        j.audio_misses > 0 ? " [miss]" : (j.audio_queue == 0 ? " [wait]" : "")
    );

    const float line_h = ImGui::GetTextLineHeight();
    const float pad_x = 5.0f, pad_y = 3.0f, gap = 2.0f;
    const float w1 = ImGui::CalcTextSize(line1).x;
    const float w2 = ImGui::CalcTextSize(line2).x;
    const float box_w = std::max(w1, w2) + pad_x * 2;
    const float box_h = line_h * 2 + gap + pad_y * 2;

    const ImVec2 box_min = {img_pos.x + 6, img_pos.y + 6};
    const ImVec2 box_max = {box_min.x + box_w, box_min.y + box_h};
    draw->AddRectFilled(box_min, box_max, IM_COL32(0, 0, 0, 185), 4.0f);

    const ImVec2 p1 = {box_min.x + pad_x, box_min.y + pad_y};
    const ImVec2 p2 = {p1.x, p1.y + line_h + gap};

    draw->AddText(p1, IM_COL32(220, 220, 220, 255), line1);

    const bool warn = j.audio_misses > 0 || j.video_misses > 0 || j.video_queue == 0 || j.audio_queue == 0;
    const ImU32 col2 = warn ? IM_COL32(255, 200, 60, 255) : IM_COL32(160, 200, 160, 255);
    draw->AddText(p2, col2, line2);
}

void UIRenderer::render_level_bar(const char* label, float level, unsigned int color) {
    ImGui::Text("%s", label);
    ImGui::SameLine();

    ImVec2 pos = ImGui::GetCursorScreenPos();
    float bw = 80.0f, bh = 12.0f;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(pos, {pos.x + bw, pos.y + bh}, IM_COL32(40, 42, 46, 255), 3.0f);
    if (level > 0.001f) {
        draw->AddRectFilled(pos, {pos.x + bw * level, pos.y + bh}, color, 3.0f);
    }

    ImGui::Dummy({bw, bh});
}
