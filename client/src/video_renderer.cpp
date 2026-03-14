#include "video_renderer.hpp"

VideoRenderer::~VideoRenderer() { cleanup(); }

void VideoRenderer::update_frame(const std::string& peer_id, const uint8_t* rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0) {
        return;
    }

    auto& info = textures_[peer_id];

    if (info.tex_id == 0) {
        glGenTextures(1, &info.tex_id);
        glBindTexture(GL_TEXTURE_2D, info.tex_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    glBindTexture(GL_TEXTURE_2D, info.tex_id);

    if (info.width != w || info.height != h) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        info.width = w;
        info.height = h;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }
}

ImTextureID VideoRenderer::texture(const std::string& peer_id) const {
    auto it = textures_.find(peer_id);
    if (it == textures_.end() || it->second.tex_id == 0) {
        return ImTextureID{};
    }
    return static_cast<ImTextureID>(it->second.tex_id);
}

ImVec2 VideoRenderer::frame_size(const std::string& peer_id) const {
    auto it = textures_.find(peer_id);
    if (it == textures_.end()) {
        return {0, 0};
    }
    return {static_cast<float>(it->second.width), static_cast<float>(it->second.height)};
}

void VideoRenderer::remove_peer(const std::string& peer_id) {
    auto it = textures_.find(peer_id);
    if (it != textures_.end()) {
        if (it->second.tex_id != 0) {
            glDeleteTextures(1, &it->second.tex_id);
        }
        textures_.erase(it);
    }
}

std::vector<std::string> VideoRenderer::active_peers() const {
    std::vector<std::string> result;
    result.reserve(textures_.size());

    for (auto& [id, _] : textures_) {
        if (!id.empty() && id[0] != '_') {
            result.push_back(id);
        }
    }
    return result;
}

void VideoRenderer::cleanup() {
    for (auto& [_, info] : textures_) {
        if (info.tex_id != 0) {
            glDeleteTextures(1, &info.tex_id);
        }
    }
    textures_.clear();
}
