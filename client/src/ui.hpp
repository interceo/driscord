#pragma once

#include <string>
#include <vector>

#include "video/screen_capture.hpp"

class App;
struct Config;

class UIRenderer {
public:
    explicit UIRenderer(const Config& cfg);

    void render(App& app);

    static void apply_discord_theme();

private:
    char server_url_buf_[256]{};
    float volume_ = 1.0f;

    std::vector<CaptureTarget> targets_;
    bool share_popup_open_ = false;
    int selected_target_ = -1;
    int selected_preset_ = 2;   // default 1080p
    int selected_fps_ = 1;      // default 30 fps

    void render_sidebar(App& app);
    void render_sidebar_bottom(App& app);
    void render_content(App& app);
    void render_share_popup(App& app);
    void render_video_panel(App& app);
    void render_level_bar(const char* label, float level, unsigned int color);
};
