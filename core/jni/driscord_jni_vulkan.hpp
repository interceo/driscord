#pragma once

#include <cstdint>
#include <string>

// Try to render a frame via an attached VulkanRenderer for the given peer.
// Returns true if a renderer was found and presentation succeeded.
// Called from the on_frame callback in driscord_jni_screen.cpp.
bool vulkan_try_present(const std::string& peer_id, const uint8_t* rgba, int w, int h);

// Destroy all attached Vulkan renderers.
void vulkan_detach_all();
