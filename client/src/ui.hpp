#pragma once

#include <string>

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

    void render_sidebar(App& app);
    void render_voice_panel(App& app);
    void render_level_bar(const char* label, float level, unsigned int color);
};
