#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include <vulkan/vulkan.h>

#include "vulkan_texture.hpp"

// Full Vulkan rendering pipeline for decoded video frames.
//
// Lifecycle:
//   1. Kotlin calls vulkanInit(windowHandle, displayHandle, w, h)
//   2. VideoReceiver calls acquire_staging() -> decode_into mapped ptr
//   3. VideoReceiver calls present() -> staging -> VkImage -> swapchain
//   4. Kotlin calls vulkanResize(w, h) on window resize
//   5. Kotlin calls vulkanDestroy() on teardown
class VulkanRenderer {
public:
    VulkanRenderer() = default;
    ~VulkanRenderer() = default;

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    bool init(uint64_t native_handle, uint64_t display_handle, int w, int h);
    void destroy();

    // Returns mapped staging pointer for direct decode.
    // Caller must write exactly w*h*4 bytes of RGBA into this buffer,
    // then call present().  Returns nullptr if texture (re)allocation fails.
    uint8_t* acquire_staging(int w, int h, size_t& capacity);

    // Flush staging -> VkImage, render textured quad, present to swapchain.
    void present(int frame_w, int frame_h);

    // Recreate swapchain (called from AWT resize on EDT).
    void resize(int w, int h);

    // Quick probe: can we create a Vulkan instance + find a physical device?
    static bool is_available();

private:
    bool create_instance();
    bool pick_physical_device();
    bool create_device();
    bool create_surface(uint64_t native_handle, uint64_t display_handle);
    bool create_swapchain(int w, int h);
    bool create_render_pass();
    bool create_descriptor_layout();
    bool create_pipeline();
    bool create_framebuffers();
    bool create_command_pool();
    bool create_sync_objects();
    bool create_sampler();
    bool create_descriptor_pool_and_set();
    void cleanup_swapchain();

    void update_descriptor_set(VkImageView view);
    void compute_letterbox(int frame_w, int frame_h, float out[4]) const;

    // Vulkan core objects
    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice phys_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    uint32_t queue_family_ = 0;

    // Surface / swapchain
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchain_format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchain_extent_ {};
    std::vector<VkImage> swapchain_images_;
    std::vector<VkImageView> swapchain_views_;
    std::vector<VkFramebuffer> framebuffers_;

    // Pipeline
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout desc_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool desc_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet desc_set_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;

    // Commands + sync
    VkCommandPool cmd_pool_ = VK_NULL_HANDLE;
    VkCommandBuffer cmd_buf_ = VK_NULL_HANDLE;
    VkSemaphore sem_image_available_ = VK_NULL_HANDLE;
    VkSemaphore sem_render_finished_ = VK_NULL_HANDLE;
    VkFence fence_in_flight_ = VK_NULL_HANDLE;

    // Textures (double-buffered)
    VulkanTexture textures_[2];
    int current_tex_ = 0;

    // Debug
#ifndef NDEBUG
    VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;
#endif

    std::mutex resize_mutex_;
};
