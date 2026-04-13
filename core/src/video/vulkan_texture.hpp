#pragma once

#include <cstddef>
#include <cstdint>

#include <vulkan/vulkan.h>

// Manages a VkImage + persistently-mapped staging buffer pair.
// The staging buffer is mapped once at init and exposed via mapped_ptr()
// so that callers (e.g. VideoDecoder::decode_into) can write directly
// into GPU-visible memory — zero intermediate copies.
class VulkanTexture {
public:
    VulkanTexture() = default;
    ~VulkanTexture() = default;

    VulkanTexture(const VulkanTexture&) = delete;
    VulkanTexture& operator=(const VulkanTexture&) = delete;

    bool init(VkDevice device,
        VkPhysicalDevice phys_device,
        VkCommandPool cmd_pool,
        VkQueue queue,
        int width,
        int height);

    void destroy();

    // Persistently mapped pointer to the staging buffer.
    // Decode directly into this: sws_scale(..., mapped_ptr(), ...).
    uint8_t* mapped_ptr() const { return mapped_; }
    size_t capacity() const { return staging_size_; }
    int width() const { return width_; }
    int height() const { return height_; }

    // Transfer staging buffer contents into the VkImage.
    // Must be called after writing into mapped_ptr().
    void flush_to_image(VkCommandBuffer cmd);

    VkImageView image_view() const { return image_view_; }

    // Transition image layout (utility, used internally and by renderer).
    static void transition_layout(VkCommandBuffer cmd,
        VkImage image,
        VkImageLayout old_layout,
        VkImageLayout new_layout);

private:
    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags props) const;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice phys_device_ = VK_NULL_HANDLE;

    // Staging buffer (host-visible, persistently mapped)
    VkBuffer staging_buf_ = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem_ = VK_NULL_HANDLE;
    uint8_t* mapped_ = nullptr;
    VkDeviceSize staging_size_ = 0;

    // GPU-local image
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory image_mem_ = VK_NULL_HANDLE;
    VkImageView image_view_ = VK_NULL_HANDLE;

    int width_ = 0;
    int height_ = 0;
    bool first_flush_ = true;
};
