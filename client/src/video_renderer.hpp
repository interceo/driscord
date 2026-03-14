#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#elif defined(_WIN32)
#include <windows.h>
#include <GL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <imgui.h>

class VideoRenderer {
public:
    ~VideoRenderer();

    // Upload an RGBA frame for a given peer (must be called on the GL thread).
    void update_frame(const std::string& peer_id, const uint8_t* rgba, int w, int h);

    ImTextureID texture(const std::string& peer_id) const;
    ImVec2 frame_size(const std::string& peer_id) const;

    void remove_peer(const std::string& peer_id);
    std::vector<std::string> active_peers() const;
    void cleanup();

private:
    struct TextureInfo {
        GLuint tex_id = 0;
        int width = 0;
        int height = 0;
    };

    std::unordered_map<std::string, TextureInfo> textures_;
};
