#pragma once

#include <string>

class App;

class UIRenderer {
public:
    UIRenderer();

    void render(App& app);

    static void apply_discord_theme();

private:
    char server_url_buf_[256] = "ws://localhost:8080";
    float volume_ = 1.0f;

    void render_sidebar(App& app);
    void render_voice_panel(App& app);
    void render_log(App& app);
    void render_level_bar(const char* label, float level, unsigned int color);
};
