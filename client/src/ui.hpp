#pragma once

#include <string>
#include <vector>

#include "app.hpp"
#include "video/screen_capture.hpp"

struct Config;

class UIRenderer {
public:
    explicit UIRenderer(const Config& cfg);

    void render(App& app);

    static void apply_discord_theme();

private:
    char server_url_buf_[256]{};
    float volume_ = 1.0f;
    std::string popup_peer_id_;
    float popup_peer_vol_ = 1.0f;
    bool popup_is_self_ = false;

    std::vector<CaptureTarget> targets_;
    bool share_popup_open_ = false;
    int selected_target_ = -1;
    StreamQuality selected_quality_ = StreamQuality::FHD_1080;
    FrameRate selected_fps_ = FrameRate::FPS_30;

    void render_sidebar(App& app);
    void render_sidebar_bottom(App& app);
    void render_content(App& app);
    void render_share_popup(App& app);
    void render_video_panel(App& app);
    void render_user_popup(App& app);
    void render_level_bar(const char* label, float level, unsigned int color);
};
