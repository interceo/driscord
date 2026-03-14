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
    int selected_target_ = 0;
    int last_preview_idx_ = -1;

    void render_sidebar(App& app);
    void render_sidebar_bottom(App& app);
    void render_content(App& app);
    void render_screen_sharing(App& app);
    void render_video_panel(App& app);
    void render_level_bar(const char* label, float level, unsigned int color);
};
