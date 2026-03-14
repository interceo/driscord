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
    style.WindowRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 3.0f;
    style.ScrollbarRounding = 4.0f;
    style.FramePadding = ImVec2(8, 4);
    style.ItemSpacing = ImVec2(8, 6);
    style.WindowPadding = ImVec2(12, 12);

    auto* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.20f, 0.21f, 0.23f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.18f, 0.19f, 0.21f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.17f, 0.18f, 0.20f, 1.00f);
    colors[ImGuiCol_Border] = ImVec4(0.14f, 0.15f, 0.16f, 1.00f);
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
    colors[ImGuiCol_Separator] = ImVec4(0.14f, 0.15f, 0.16f, 1.00f);
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

    float sidebar_width = 220.0f;

    ImGui::BeginChild("##Sidebar", ImVec2(sidebar_width, 0), ImGuiChildFlags_Borders);
    render_sidebar(app);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##Content", ImVec2(0, 0));
    render_voice_panel(app);
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
        if (ImGui::Button("Connect", ImVec2(-1, 32))) {
            app.connect(server_url_buf_);
        }
    } else {
        ImVec4 red(0.85f, 0.25f, 0.25f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, red);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.20f, 0.20f, 1.0f));
        if (ImGui::Button("Disconnect", ImVec2(-1, 32))) {
            app.disconnect();
        }
        ImGui::PopStyleColor(3);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (state == AppState::Connecting) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Connecting...");
    } else if (state == AppState::Connected) {
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "Connected");
        std::string short_id = app.local_id();
        ImGui::TextDisabled("ID: %s", short_id.c_str());
    } else {
        ImGui::TextDisabled("Disconnected");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Text("Members");

    auto peers = app.peers();
    if (peers.empty() && state != AppState::Connected) {
        ImGui::TextDisabled("  —");
    } else {
        if (state == AppState::Connected) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "  %s (you)", app.local_id().c_str());
        }
        for (auto& p : peers) {
            ImVec4 col = p.connected ? ImVec4(0.2f, 0.9f, 0.3f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
            ImGui::TextColored(col, "  %s", p.id.c_str());
        }
    }
}

void UIRenderer::render_voice_panel(App& app) {
    ImGui::Text("Voice Channel");
    ImGui::Spacing();

    bool connected = app.state() == AppState::Connected;

    if (!connected) {
        ImGui::TextDisabled("Connect to a server to start voice chat");
        return;
    }

    bool muted = app.muted();
    if (muted) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.20f, 0.20f, 1.0f));
    }
    if (ImGui::Button(muted ? "Unmute" : "Mute", ImVec2(200, 32))) {
        app.toggle_mute();
    }
    if (muted) {
        ImGui::PopStyleColor(3);
    }

    ImGui::Spacing();

    volume_ = app.volume();
    ImGui::SetNextItemWidth(200);
    if (ImGui::SliderFloat("Volume", &volume_, 0.0f, 2.0f, "%.1f")) {
        app.set_volume(volume_);
    }

    ImGui::Spacing();

    float in_level = std::min(app.input_level() * 5.0f, 1.0f);
    float out_level = std::min(app.output_level() * 5.0f, 1.0f);

    render_level_bar("Mic    ", in_level, muted ? 0xFF4444AA : 0xFF44AA44);
    render_level_bar("Speaker", out_level, 0xFF44AA44);
}

void UIRenderer::render_level_bar(const char* label, float level, unsigned int color) {
    ImGui::Text("%s", label);
    ImGui::SameLine();

    ImVec2 pos = ImGui::GetCursorScreenPos();
    float bar_width = 200.0f;
    float bar_height = 14.0f;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(pos, ImVec2(pos.x + bar_width, pos.y + bar_height), IM_COL32(40, 42, 46, 255), 3.0f);
    if (level > 0.001f) {
        draw->AddRectFilled(pos, ImVec2(pos.x + bar_width * level, pos.y + bar_height), color, 3.0f);
    }

    ImGui::Dummy(ImVec2(bar_width, bar_height));
}
